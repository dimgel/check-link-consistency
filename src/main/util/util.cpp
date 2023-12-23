#include <algorithm>
#include <fcntl.h>
#include <fstream>
#include <linux/limits.h>
#include <optional>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "Error.h"
#include "Finally.h"
#include "StdCapture.h"
#include "util.h"

#define FILE_LINE "util:" LINE ": "


namespace dimgel::util {

	StringRef concatStringViews(char* buf, size_t bufSize, std::initializer_list<std::string_view> sources) {
		if (bufSize == 0) {
			// No place even for terminating '\0'.
			throw std::runtime_error(FILE_LINE "concatStringViews(): buffer is empty");
		}
		char* b = buf;
		size_t nTotal = 0;
		for (auto& src : sources) {
			size_t n = src.length();
			nTotal += n;
			if (nTotal >= bufSize) {
				throw std::runtime_error(FILE_LINE "concatStringViews(): buffer overflow");
			}
			memcpy(b, src.data(), n);
			b += n;
		}
		*b = '\0';
		return StringRef::createUnsafe(buf, nTotal);
	}


	static constinit std::string_view trimInplace_spaces(" \t\r\n");
	void trimInplace(std::string_view& s) {
		if (s.empty()) {
			return;
		}
		auto i = s.find_first_not_of(trimInplace_spaces);
		if (i == std::string::npos) {
			s.remove_prefix(s.size());
		} else {
			s.remove_prefix(i);
			s.remove_suffix(s.size() - s.find_last_not_of(trimInplace_spaces) - 1);
		}
	}

	std::string regex_escape(const std::string& s) {
		// Adopted from: https://stackoverflow.com/a/39237913/4247442
		static const char metachars[] = R"(\.^$-+()[]{}|?*)";
		std::string x;
		x.resize(s.size() * 2);
		char* xp = x.data();
		for (auto c : s) {
			if (strchr(metachars, c)) {
				*xp++ = '\\';
			}
			*xp++ = c;
		}
		x.resize(xp - x.data());
		return x;
	}


	std::regex pathWildcardsToRegex(const std::string& s) {
		static const std::regex r2("\\\\\\*\\\\\\*");
		static const std::regex r1("\\\\\\*");
		static const std::regex rq("\\\\\\?");

		std::string x = "^" + regex_escape(s) + "$";
		x = std::regex_replace(x, r2, ".*");
		x = std::regex_replace(x, r1, "[^/]*");
		x = std::regex_replace(x, rq, "[^/]");
		return std::regex {x};
	}


	bool realPath(const char* path, char* buf) {
		if (::realpath(path, buf) != nullptr) {
			return true;
		} else if (errno == ENOENT) {
			return false;
		} else {
			throw Error(FILE_LINE "::realpath(`%s`) failed: %s", path, strerror(errno));
		}
	}

//	#define DBG printf("line=%d, pi=%d, bi=%d \n", __LINE__, pi, bi);
	size_t normalizePath(const char* path, char* buf) {
		if (!path || !buf) {
			throw std::runtime_error(FILE_LINE "normalizePath(): null argument(s)");
		}
		int pi = 0;
		int bi = 0;

		// Process absoulte path.
		if (path[0] == '/') {
			buf[0] = '/';
			pi++;
			bi++;
		}

		while (true) {
			// Now we are at beginning of next path component.

			// Skip empty path components.
			while (path[pi] == '/') {
				pi++;
			}

			if (path[pi] == '.') {
				if (path[pi + 1] == '\0') {
					goto skipLastDots;
				}
				if (path[pi + 1] == '/') {
					pi += 2;
					continue;
				}
				char c;
				if (path[pi + 1] == '.' && ((c = path[pi + 2]) == '/' || c == '\0')) {
					// Process "/../" or trailing "/..".
					if (bi < 2) {
						// We are parsing first path component, it cannot be "..".
						throw Error(FILE_LINE "normalizePath(`%s`): path beyond root", path);
					}
					// Point bi at '/' after previous path component.
					bi--;
					// Point bi at first char of previous path component.
					while (true) {
						bi--;
						if (buf[bi] == '/') {
							bi++;
							break;
						}
						if (bi == 0) {
							break;
						}
					}
					if (c == '\0') {
						goto skipLastDots;
					} else {
						pi += 3;
						continue;
					}
				}
			}

			// Copy path compnent and following '/'.
			while (true) {
				char c = buf[bi] = path[pi];
				if (c == '\0') {
					return bi;
				}
				pi++;
				bi++;
				if (c == '/') {
					break;
				}
			}
		} // while (true)

	skipLastDots:
		// Skip trailing "/." or "/..".
		if (bi > 1) {
			// Delete last '/' unless it's absolute path's root.
			bi--;
		}
		buf[bi] = '\0';
		return bi;
	}


	statx_Result statx(const char* path) {
		struct statx st;
		constexpr decltype(st.stx_mask) mask = STATX_TYPE | STATX_MODE | STATX_INO;
		if (::statx(AT_FDCWD, path, AT_NO_AUTOMOUNT | AT_SYMLINK_NOFOLLOW, mask, &st) == -1) {
			throw Error(FILE_LINE "::statx(`%s`) failed: %s", path, strerror(errno));
		}
		if ((st.stx_mask & mask) != mask) {
			throw Error(FILE_LINE "::statx(`%s`) returned incomplete data; unsupported filesystem?", path);
		}
		return {.mode = st.stx_mode, .inode = st.stx_ino};
	}


	void scanDir(const char* path, std::function<void(const struct dirent&)> callback) {
		DIR* d = opendir(path);
		if (d == nullptr) {
			if (errno == ENOENT) {
				// Not found -- it's OK.
				return;
			} else {
				throw Error(FILE_LINE "::opendir(`%s`) failed: %s", path, strerror(errno));
			}
		}

		Finally dFin([&] {
			closedir(d);
		});

		dirent* de;
		while ((de = readdir(d)) != nullptr) {
			if (de->d_name[0] == '.' && (de->d_name[1] == '.' || de->d_name[1] == '\0')) {
				continue;
			}
			if (de->d_type == DT_UNKNOWN) {
				// `man 3 readdir`: not all filesystems support d_type.
				// Since I don't want additional statx() syscall on each direntry, there's no reason to continue.
				throw Error(
					FILE_LINE "Could not read directory `%s`: unsupported filesystem: got direntry `%s` with d_type = DT_UNKNOWN",
					path, de->d_name
				);
			}
			callback(*de);
		}
	}


	// https://stackoverflow.com/a/2602060
	BufAndRef readFile(const char* path) {
		Closeable fd {open(path, O_RDONLY)};
		if (fd < 0) {
			throw Error(FILE_LINE "readFile(`%s`): open() failed: %s", path, strerror(errno));
		}
		auto ssize = lseek(fd, 0, SEEK_END);
		if (ssize < 0) {
			throw Error(FILE_LINE "readFile(`%s`): lseek() failed: %s", path, strerror(errno));
		}
		if (lseek(fd, 0, SEEK_SET) < 0) {
			throw Error(FILE_LINE "readFile(`%s`): lseek() failed: %s", path, strerror(errno));
		}

		size_t size = (size_t)ssize;
		auto buf = std::make_unique<char[]>(size + 1);
		char* s = buf.get();

		auto n = read(fd, s, size);
		if (n < 0) {
			throw Error(FILE_LINE "readFile(`%s`): read() failed: %s", path, strerror(errno));
		}
		if ((size_t)n != size) {
			throw Error(FILE_LINE "readFile(`%s`): read wrong number of bytes", path);
		}
		s[size] = '\0';

		return {.buf {std::move(buf)}, .ref {StringRef::createUnsafe(s, size)}};
	}


	int forkExec(const char* argv[], bool requireStatus0) {
		int pid = fork();
		if (pid == -1) {
			throw Error("fork() failed: %s", strerror(errno));
		}
		if (pid == 0) {
			// const_cast is OK: https://stackoverflow.com/a/190208
			execv(argv[0], const_cast<char**>(argv));
			throw Error("exec(`%s`) failed: %s", argv[0], strerror(errno));
		}
		int wstatus;
		int x = waitpid(pid, &wstatus, 0);
		if (x == -1) {
			throw Error("waitpid() failed: %s", strerror(errno));
		}
		if (!WIFEXITED(wstatus)) {
			// `man 2 wait`: "By default, waitpid() waits only for terminated children"
			throw Error("`%s` aborted", argv[0]);
		}
		int status = WEXITSTATUS(wstatus);
		if ((requireStatus0 && status != 0)) {
			throw Error("`%s` exited with status %d", argv[0], status);
		}
		return status;
	}


	forkExecStdCapture_Result forkExecStdCapture(const char* argv[], forkExecStdCapture_Params p) {
		auto captureStdOut {p.captureStdOut ? std::optional{StdCapture::createStdOut(true)} : std::nullopt};
		auto captureStdErr {p.captureStdErr ? std::optional{StdCapture::createStdErr(true)} : std::nullopt};

		int pid = fork();
		if (pid == -1) {
			throw Error("fork() failed: %s", strerror(errno));
		}

		if (pid == 0) {
			if (captureStdOut) captureStdOut->initChildProcess();
			if (captureStdErr) captureStdErr->initChildProcess();

			// ATTENTION!!! Capture IS NOT destroyed here, because destructor restores child's stdout & stderr.
			// const_cast is OK: https://stackoverflow.com/a/190208
			execv(argv[0], const_cast<char**>(argv));
			throw Error("exec(`%s`) failed: %s", argv[0], strerror(errno));
		}

		if (captureStdOut) captureStdOut->initParentProcess();
		if (captureStdErr) captureStdErr->initParentProcess();

		forkExecStdCapture_Result result;
		std::ostringstream osOut;
		std::ostringstream osErr;
		while (true) {
			// In linux, I cannot wait on both pid and stdout/stderr with single syscall, so using usleep().

			// Non-blocking:
			if (captureStdOut) captureStdOut->get(osOut);
			if (captureStdErr) captureStdErr->get(osErr);

			int wstatus;
			int x = waitpid(pid, &wstatus, WNOHANG);
			if (x == -1) {
				throw Error("waitpid() failed: %s", strerror(errno));
			}
			if (x == 0) {
				// WNOHANG was given, no status changes.
				continue;
			}
			if (WIFEXITED(wstatus)) {
				result.status = WEXITSTATUS(wstatus);
				if ((p.requireStatus0 && result.status != 0)) {
					throw Error("`%s` exited with status %d", argv[0], result.status);
				} else {
					break;
				}
			}
			if (WIFSIGNALED(wstatus) || WIFSTOPPED(wstatus)) {
				throw Error("`%s` aborted", argv[0]);
			}

			usleep(1000);   // 1ms (in microseconds).
		}

		if (captureStdOut) captureStdOut->get(osOut);
		if (captureStdErr) captureStdErr->get(osErr);
		result.stdOut = osOut.str();
		result.stdErr = osErr.str();
		return result;
	}
}
