#include <archive_entry.h>
#include <filesystem>
#include <functional>
#include <map>
#include <ranges>
#include <sys/stat.h>
#include "PacMan_Arch.h"
#include "util/ArchiveReader.h"
#include "util/Error.h"
#include "util/Log.h"
#include "util/SplitMutableString.h"
#include "util/ThreadPool.h"
#include "util/util.h"

namespace fs = std::filesystem;

#define FILE "PacMan_Arch:"
#define FILE_LINE FILE LINE ": "


namespace dimgel {

	void PacMan_Arch::iterateInstalledPackages(std::function<void(std::string installedPackageUniqueID)> f) {
		util::scanDir(installedInfoPath.c_str(), [&](const dirent& de) {
			if (de.d_type == DT_DIR) {
				f(de.d_name);
			}
		});
	}


	//----------------------------------------------------------------------------------------------------------------------------------------


	PacMan::parseInstalledPackage_Result PacMan_Arch::parseInstalledPackage(const std::string& installedPackageUniqueID) {
		PacMan::parseInstalledPackage_Result result;
		result.p = Package::create(ctx.mm);

		auto& dirName = installedPackageUniqueID;
		char dirPathBuf[PATH_MAX];
		auto dirPath = util::concatStringViews(dirPathBuf, sizeof(dirPathBuf), {installedInfoPath.c_str(), "/", dirName.c_str()});

		struct openFile_Result {
			StringRef path;
			BufAndRef contents;
		};
		auto openFile = [&](const char* fileName, char filePathBuf[PATH_MAX]) {
			auto filePath = util::concatStringViews(filePathBuf, PATH_MAX, {dirPath.sv(), "/", fileName});
			if (!fs::is_regular_file(filePath.sv())) {
				throw Error(FILE_LINE "read `%s`: not a regular file", filePathBuf);
			}
			auto contents = util::readFile(filePath.cp());
			return openFile_Result{.path = filePath, .contents = std::move(contents)};
		};

		auto readFile = [&](const char* fileName) {
			char filePathBuf[PATH_MAX];
			auto file = openFile(fileName, filePathBuf);
			SplitMutableString lines(file.contents.ref);

			auto getLine = [&](SplitMutableString::ConstIterator& it) {
				if (it == it.getOwner().end()) {
					throw Error(FILE_LINE "read `%s` line %d: unexpected EOF", filePathBuf, it.getPartNo());
				}
				return *it++;
			};
			auto skipEmptyLine = [&](SplitMutableString::ConstIterator& it) {
				if (it == it.getOwner().end()) {
					throw Error(FILE_LINE "read `%s` line %d: expected empty line, got EOF", filePathBuf, it.getPartNo());
				}
				if (!it->empty()) {
					throw Error(FILE_LINE "read `%s` line %d: expected empty line", filePathBuf, it.getPartNo());
				}
				++it;
			};

			for (auto it = lines.begin();  it != lines.end();  ) {
				auto sv = *it++;
				if (sv.empty()) {
					// Skipping leading & multiple empty lines.
				} else if (sv == "%NAME%") {
					result.p->name = alloc::String{ctx.mm, getLine(it)};
					skipEmptyLine(it);
				} else if (sv == "%VERSION%") {
					result.p->version = alloc::String{ctx.mm, getLine(it)};
					skipEmptyLine(it);
				} else if (sv == "%PROVIDES%") {
					while (!(sv = getLine(it)).empty()) {
						result.p->provides.insert(alloc::String{ctx.mm, sv});
					}
				} else if (sv == "%OPTDEPENDS%") {
					while (!(sv = getLine(it)).empty()) {
						auto i = sv.find(':');
						result.p->optDepends.insert(alloc::String{ctx.mm, i == std::string::npos ? sv.sv() : sv.substr(0, i)});
					}
				} else if (sv == "%FILES%") {
					while (!(sv = getLine(it)).empty()) {
						// Filter out directories.
						if (!sv.ends_with('/')) {
							// Pacman assumes these are real paths without leading '/' (root prefix), see notes/sources-pacman.txt.
							result.filePaths1.insert(alloc::String{ctx.mm, sv});
						}
					}
				} else if (sv[0] == '%') {
					while (!(sv = getLine(it)).empty()) {
						// Skip unknown section, make sure it also ends with empty line.
					}
				} else {
					throw Error(FILE_LINE "read `%s` line %d: expected %%SECTION_NAME%%", filePathBuf, it.getPartNo() - 1);
				}
			}
		};

		// Pacman reads both files in single function, so do I.
		// NOTE: Considered reading `mtree` file instead of `files` so I could filter in only regular files with x-permission, *.so[.*] extension and symlinks;
		//       But `mtree` files are gzipped and still they are larger than `files`, hence no profit.
		readFile("desc");
		readFile("files");

		char packageNameVer[200];
		if (util::concatStringViews(packageNameVer, sizeof(packageNameVer), {result.p->name.sv(), "-", result.p->version.s()}) != dirName) {
			throw Error(FILE_LINE "read `%s`: sanity check failed: package.name + '-' + package.version != dirName", dirPath.cp());
		}

		return result;
	} // PacMan_Arch::parseInstalledPackage()


	//----------------------------------------------------------------------------------------------------------------------------------------


	void PacMan_Arch::downloadOptionalDependencies_impl() {
		// 1. Download optDeps without installing. Split too large command line into multiple exec() calls.
		// "POSIX suggests to subtract 2048 additionally so that the process may savely modify its environment." (c) https://stackoverflow.com/a/14419676
		if (!ctx.noNetwork) {
			long argsMaxLength {sysconf(_SC_ARG_MAX) - 2048};
			if (argsMaxLength < 0) {
				throw Error(FILE_LINE "sysconf() failed: %s", strerror(errno));
			}
			std::vector<const char*> argv;
			argv.reserve(data.optDependsSorted.size());
			for (auto it = data.optDependsSorted.begin();  it != data.optDependsSorted.end();  ) {
				{
					long argsCurLength = 0;

					auto addArg = [&](StringRef s) -> bool {
						if (argsCurLength + (long)s.size() + 1 > argsMaxLength) {
							return false;
						}
						argv.push_back(s.cp());
						argsCurLength += s.size() + 1;
						return true;
					};

					argv.clear();
					addArg("/usr/bin/pacman");
					addArg("-Sw");
					addArg(ctx.colorize ? "--color=always" : "--color=never");
					addArg("--noconfirm");
					while (it != data.optDependsSorted.end() && addArg(*it)) {
						++it;
					}
					argv.push_back(nullptr);

					if (ctx.verbosity >= Verbosity_WarnAndExec) {
						std::ostringstream os;
						os << argv[0];
						for (size_t i = 1;  i < argv.size() - 1;  i++) {
							os << ' ' << argv[i];
						}
						ctx.log.exec("%s", os.str().c_str());
					}
				}

				try {
					util::forkExecStdCapture(argv.data(), {.requireStatus0 = true, .captureStdOut = ctx.verbosity < Verbosity_WarnAndExec, .captureStdErr = false});
				} catch (std::exception& e) {
					// Pacman may fail to complete transaction because user added some dependencies or their sub-dependencies to IgnorePkg in /etc/pacman.conf,
					// but more likely user pressed Ctrl+C. So, `break` instead of `continue`: don't download rest of optdeps.
					throw Error(
						FILE_LINE "exec(pacman -Sw) failed: %s"
								"\n      Check IgnorePkg in /etc/pacman.conf."
								"\n      Aborting: downloaded archives can be damaged.",
						e.what()
					);
				}
			} // for (... packages.packagesByOptDepend ...)
		} // if (!ctx.noNetwork)


		// 2. For each successfully downloaded optdep, ask pacman about its package name and archive file name: exec `pacman -Swp ... {optDep}`.
		// Pacman can translate dependency name to package name (e.g. nvidia-utils=495.44 ---> nvidia-utils, libasound.so=2-64 ---> alsa-lib),
		// and I cannot guess package name from dependency name myself. Thus, separate forkExec() call for each package.
		class FindArchiveTask : public ThreadPool::Task {
			PacMan_Arch& owner;
			alloc::String optDepName;
			alloc::String& archiveName;   // Mutable reference.

		public:
			FindArchiveTask(PacMan_Arch& owner, alloc::String optDepName, alloc::String& archiveName)
			    : owner(owner), optDepName(optDepName), archiveName(archiveName) {}

			void compute() override {
				const char* argvColor = owner.ctx.colorize ? "--color=always" : "--color=never";
				const char* argv[] = {
					"/usr/bin/pacman",
					"-Sw",
					argvColor,
					"--print-format",
					"%n %l",
					optDepName.cp(),
					nullptr
				};
				if (owner.ctx.verbosity >= Verbosity_WarnAndExec) {
					owner.ctx.log.exec("/usr/bin/pacman -Sw %s --print-format '%%n %%l' %s", argvColor, optDepName.cp());
				}
				util::forkExecStdCapture_Result x;
				try {
					x = util::forkExecStdCapture(argv, {.requireStatus0 = true, .captureStdOut = true, .captureStdErr = false});
				} catch (std::exception& e) {
					owner.ctx.log.error("skipping optional dependency `%s`: exec() failed: %s", optDepName.cp(), e.what());
					return;
				}

				// Many commands will output multiple lines (including dependencies).
				SplitMutableString lines(x.stdOut);
				std::cmatch m;
				std::string_view m2;
				auto it = lines.begin();
				for (;  it != lines.end();  ++it) {
					if (!util::regex_match(*it, m, owner.rPacmanSWPLine)) {
						owner.ctx.log.error("skipped optional dependency `%s`: couldn't parse exec() output line %d", optDepName.cp(), it.getPartNo());
						return;
					}
					if (m[1] == optDepName) {
						m2 = {m[2].first, m[2].second};
						break;
					}
				}
				if (m2.empty()) {
					if (it.getPartNo() == 1) {
						owner.ctx.log.error("skipped optional dependency `%s`: exec() output is empty", optDepName.cp());
						return;
					}
					m2 = {m[2].first, m[2].second};
					if (it.getPartNo() > 2 && owner.ctx.verbosity >= Verbosity_WarnAndExec) {
						// This is OK until PacMan::ParseArchiveTask::compute() finds out that chosen package does not match optDepName.
						owner.ctx.log.warn(
							FILE_LINE "rewritten optional dependency `%s` ---> `%s`: exec() output has multiple lines without exact match, took last line",
							optDepName.cp(), m[1].str().c_str()
						);
					}
				}
				if (!m2.starts_with(owner.archivesURL) || m2.length() == owner.archivesURL.length()) {
					// In multiline output, some sub-dependencies may be outdated and have URL "http://..." instead of "file:///var/cache/pacman/pkg/...".
					// I don't care of them as long as requested dependency itself is downloaded. So I postponed this check until I found exact line I need.
					owner.ctx.log.error(
						"skipped optional dependency `%s`: couldn't parse URL `%s`: expected `file:///...`",
						optDepName.cp(), std::string(m2).c_str()
					);
					return;
				}
				archiveName = alloc::String{owner.ctx.mm, m2.substr(owner.archivesURL.length())};
				if (owner.ctx.verbosity >= Verbosity_Debug) {
					owner.ctx.log.debug(
						FILE_LINE "resolved `%s` ---> `%s%s`",
						optDepName.cp(), owner.archivesPath.c_str(), archiveName.cp()
					);
				}
			}

			void merge() override {
			}
		};


		std::vector<std::unique_ptr<ThreadPool::Task>> tasks;
		tasks.reserve(data.archiveNamesByOptDepend.size());
		for (auto& [optdep, archiveName] : data.archiveNamesByOptDepend) {
			tasks.push_back(std::make_unique<FindArchiveTask>(*this, optdep, archiveName));
		}
		ctx.threadPool.addTasks(ctx.threadPool.groupTasks(std::move(tasks)));
		ctx.threadPool.waitAll();

	} // PacMan_Arch::downloadOptionalDependencies_impl()


	//----------------------------------------------------------------------------------------------------------------------------------------


	void PacMan_Arch::ParseArchiveTask::impl(Package* p) {
		ArchiveReader a(owner.archivesPath + archiveName.cp());

		// Pass 1/2: all symlinks. See comments in PacMan.h.
		a.scanAll([&](archive_entry* e) {
			if (archive_entry_filetype(e) == AE_IFLNK) {
				onSymlink(archive_entry_pathname(e), archive_entry_symlink(e));
			}
		});
		onSymlinksDone();

		// Pass 2/2: all regular files.
		a.scanAll([&](archive_entry* e) {
			auto t = archive_entry_filetype(e);
			if (t != AE_IFREG) {
				return;
			}

			const char* path = archive_entry_pathname(e);
			if (!strcmp(path, ".PKGINFO")) {
				auto bufAndRef = a.getEntryData(e);
				SplitMutableString lines(bufAndRef.ref);
				for (auto it = lines.begin();  it != lines.end();  ++it) {
					auto sv = *it;
					if (sv.empty() || sv.starts_with('#')) {
						continue;
					}
					std::cmatch m;
					if (!util::regex_match(sv, m, owner.rPkginfoSWPLine)) {
						throw Error(FILE_LINE "ignore `%s` / `.PKGINFO` line %d: failed to parse", archiveName.cp(), it.getPartNo());
					}
					if (m[1] == "pkgname") {
						std::string_view m2v {m[2].first, m[2].second};
						util::trimInplace(m2v);
						p->name = alloc::String{owner.ctx.mm, m2v};
					} else if (m[1] == "pkgver") {
						std::string_view m2v {m[2].first, m[2].second};
						util::trimInplace(m2v);
						p->version = alloc::String{owner.ctx.mm, m2v};
					} else if (m[1] == "provides") {
						p->provides.insert(alloc::String{owner.ctx.mm, util::trim(m[2])});
					}
				}
			} else if (onFileIsNeeded(path)) {
				auto bufAndRef = a.getEntryData(e);
				onFileContents(path, bufAndRef.buf.get(), bufAndRef.ref.sr.size());
			}
		});
	}


	//----------------------------------------------------------------------------------------------------------------------------------------


	std::unique_ptr<PacMan> createPacMan(Context& ctx, Data& data, ELFInspector& elfInspector) {
		return std::make_unique<PacMan_Arch>(ctx, data, elfInspector);
	}
}
