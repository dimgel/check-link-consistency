#include <assert.h>
#include <linux/limits.h>
#include <stdlib.h>
#include <string.h>
#include "ELFInspector.h"
#include "FilesCollector.h"
#include "util/Error.h"
#include "util/Log.h"
#include "util/SplitMutableString.h"
#include "util/util.h"

#define FILE "FilesCollector:"
#define FILE_LINE FILE LINE ": "


namespace dimgel {

	File* FilesCollector::processRegularFileAfterStatx(const char* path1, size_t regNameOffset, size_t length, decltype(stat::st_mode) st_mode, const char* reason) {
		bool isSecure = st_mode & (S_ISUID | S_ISGID);
		bool hasXPermisson = st_mode & (S_IXUSR | S_IXGRP | S_IXOTH);


		auto addFile = [&](const char* reason) {
			auto it = data.uniqueFilesByPath1.find(path1);
			if (it != data.uniqueFilesByPath1.end()) {
				// This is ok: same file can be found while scanning filesystem or by realpath(symlink).
				if (ctx.verbosity >= Verbosity_Debug) {
					ctx.log.debug(FILE_LINE "skip `/%s`: already added", path1);
				}
				return it->second;
			}

			File* f = File::create(ctx.mm);
			f->path1 = alloc::String{ctx.mm, path1};
			f->isSecure = isSecure;

			// So I calculate `path1` hash and search this map twice, but avoid 3772 extra String constructions...
			data.uniqueFilesByPath1.insert({f->path1, f});

			if (!allFilesByPath1.insert({f->path1, f}).second) {
				throw Error(FILE_LINE "internal error: duplicate allFilesByPath1 key `%s`", f->path1.cp());
			}
			if (ctx.verbosity >= Verbosity_Debug) {
				ctx.log.debug(FILE_LINE "add `/%s`: %s", path1, reason);
			}
			uniqueFilesAddedByCurrentIteration.push_back(f);
			return f;
		};


		if (reason != nullptr) {
			return addFile(reason);
		}
		if (hasXPermisson) {
			return addFile("has x-permission");
		}
		std::string_view regName(path1 + regNameOffset, length - regNameOffset);
		if (util::regex_match(regName, rLibName)) {
			return addFile("matches .so regex");
		}

		return nullptr;
	}


	// Param `path1` must be mutable buffer: char[PATH_MAX].
	void FilesCollector::processRecursive(char* path1, size_t regNameOffset, size_t length, ino_t dirInode, uint8_t d_type) {
		for (auto& [r, configLine] : ctx.ignoreFiles) {
			if (std::regex_match(path1, path1 + length, r)) {
				if (ctx.verbosity >= Verbosity_Debug) {
					ctx.log.debug(FILE_LINE "ignore `/%s`: by config line %d", path1, configLine);
				}
				return;
			}
		}
		switch (d_type) {
			case DT_REG: {
				auto st = util::statx(path1);
				processRegularFileAfterStatx(path1, regNameOffset, length, st.mode);
				return;
			}

			case DT_DIR: {
				// Already checked (inode == 0) in main.cpp's config reader.
				if (!processedDirs.insert(dirInode).second) {
					if (ctx.verbosity >= Verbosity_Debug) {
						ctx.log.debug(FILE_LINE "skip `/%s`: already scanned", path1);
					}
					return;
				}

				if (ctx.verbosity >= Verbosity_Debug) {
					ctx.log.debug(FILE_LINE "scan `/%s`", path1);
				}
				path1[length] = '/';
				path1[length + 1] = '\0';
				util::scanDir(path1, [&](const struct dirent& de) {
					size_t nameOffset = length + 1;
					strcpy(path1 + nameOffset, de.d_name);
					processRecursive(path1, nameOffset, nameOffset + strlen(path1 + nameOffset), de.d_ino, de.d_type);
				});
				path1[length] = '\0';
				return;
			}

			// See "On symlinks" in notes/decisions.txt.
			case DT_LNK: {
				char resolvedPath0[PATH_MAX];
				if (!util::realPath(path1, resolvedPath0)) {
					if (ctx.verbosity >= Verbosity_Debug) {
						ctx.log.debug(FILE_LINE "skip `/%s`: orphan symlink", path1);
					}
					return;
				}
				auto st = util::statx(resolvedPath0);
				if (S_ISREG(st.mode)) {
					StringRef sv(resolvedPath0 + 1);
					File* f = processRegularFileAfterStatx(sv.cp(), sv.rfind('/') + 1, sv.length(), st.mode);
					if (f != nullptr) {
						if (!allFilesByPath1.insert({alloc::String{ctx.mm, path1}, f}).second) {
							throw Error(FILE_LINE "internal error: duplicate allFilesByPath1 key `%s`", path1);
						}
						if (ctx.verbosity >= Verbosity_Debug) {
							ctx.log.debug(FILE_LINE "add `/%s`: symlink to `%s`", path1, resolvedPath0);
						}
					} else {
						if (ctx.verbosity >= Verbosity_Debug) {
							ctx.log.debug(FILE_LINE "skip `/%s`: symlink to skipped `%s`", path1, resolvedPath0);
						}
					}
				} else if (S_ISDIR(st.mode)) {
					if (ctx.verbosity >= Verbosity_Debug) {
						ctx.log.debug(FILE_LINE "follow `/%s`: symlink to dir `%s`", path1, resolvedPath0);
					}
					processRecursive(resolvedPath0 + 1, 0, strlen(resolvedPath0 + 1), st.inode, DT_DIR);
				}
				return;
			}

			default: {
				return;
			}
		}
	}


	void FilesCollector::processQueue() {
		// ELFInspector can call scanAdditionalDir() for RPATH and RUNPATH entries, so loop until no more paths to scan.
		while (true) {
			while (!queue.empty()) {
				SearchPath sp = queue.front();
				queue.pop();
				char path1[PATH_MAX];
				strcpy(path1, sp.path1.cp());
				processRecursive(path1, 0, sp.path1.sv().length(), sp.inode, DT_DIR);
			}


			if (ctx.verbosity >= Verbosity_Debug) {
				ctx.log.debug(FILE_LINE "stats: uniqueFilesAddedByCurrentIteration.size() = %lu", ulong{uniqueFilesAddedByCurrentIteration.size()});
			}
			if (uniqueFilesAddedByCurrentIteration.empty()) {
				break;
			}


			class Task : public ThreadPool::Task {
				FilesCollector& owner;
				File& f;
			public:
				Task(FilesCollector& owner, File& f) : owner(owner), f(f) {}
				void compute() override {

					// Inspect.
					// --------

					owner.elfInspector.processOne_file(f, [&](SearchPath p) { owner.scanAdditionalDir(p); });
					if (!f.isDynamicELF) {
						return;
					}

					// Assign package & apply config.
					// ------------------------------

					auto addLibsAndPaths = [&](std::vector<AddLibPath>& addList) {
						for (AddLibPath& add : addList) {
							SearchPath sp {.path1 = add.path0.substr(1), .inode = add.inode};
							f.configPaths.push_back(sp);
							if (sp.inode != 0) {
								// 0 means directory does not exist, and was kept for optdeps.
								owner.scanAdditionalDir(sp);
							}
							if (owner.ctx.verbosity >= Verbosity_Debug) {
								owner.ctx.log.debug(FILE_LINE "`/%s`: add search path from config line %d: `%s`", f.path1.cp(), add.configLineNo, sp.path1.cp());
							}
						}
					};

					if (auto it = owner.data.packagesByFilePath1.find(f.path1);  it != owner.data.packagesByFilePath1.end()) {
						Package* p = f.belongsToPackage = it->second;
						if (owner.ctx.verbosity >= Verbosity_Debug) {
							owner.ctx.log.debug(FILE_LINE "`/%s`: assign package `%s %s`", f.path1.cp(), p->name.cp(), p->version.cp());
						}
						// Apply per-package configuration.
						if (auto it2 = owner.ctx.addLibPathsByPackage.find(p);  it2 != owner.ctx.addLibPathsByPackage.end()) {
							addLibsAndPaths(it2->second);
						}
					}

					// Apply per-filename configuration.
					for (auto& [path1Pfx, addList] : owner.ctx.addLibPathsByFilePath1Prefix) {
						if (f.path1 == path1Pfx || f.path1.sv().starts_with(path1Pfx.sv())) {
							addLibsAndPaths(addList);
						}
					}
				}

				void merge() override {
				}
			};


			std::vector<std::unique_ptr<ThreadPool::Task>> tasks;
			tasks.reserve(uniqueFilesAddedByCurrentIteration.size());
			for (File* f : uniqueFilesAddedByCurrentIteration) {
				tasks.push_back(std::make_unique<Task>(*this, *f));
			}
			ctx.threadPool.addTasks(ctx.threadPool.groupTasks(std::move(tasks)));
			ctx.threadPool.waitAll();
			uniqueFilesAddedByCurrentIteration.clear();
		}
	}


	void FilesCollector::execute() {
		if (ctx.verbosity >= Verbosity_Default) {
			ctx.log.info("Scanning filesystem for bins & libs...");
		}

		// Scan filesystem.
		// ----------------
		{
			// Values are based on my system's current stats dumped below, with ~1.5x reserve.
			data.uniqueFilesByPath1.reserve(17600);
			data.libs.reserve(20800);
			processedDirs.reserve(14200);
			allFilesByPath1.reserve(23300);
			uniqueFilesAddedByCurrentIteration.reserve(16500);
			// Lots of ELF-s contain "/usr/bin" in RPATH/RUNPATH; 550 on my system: check-link-consistency -vv | grep -F '`/usr/lib`' | grep -P 'RPATH|RUNPATH'
			// TODO No such method; specify explicit backing collection?
//			queue.reserve(1000);

			for (auto& searchPath : ctx.scanBins       ) { queue.push(searchPath); }
			for (auto& searchPath : ctx.scanDefaultLibs) { queue.push(searchPath); }
			for (auto& searchPath : ctx.scanMoreLibs   ) { queue.push(searchPath); }

			processQueue();
		}


		// Call `ldconfig -p` and fill ldCache
		// -----------------------------------
		{
			if (ctx.verbosity >= Verbosity_WarnAndExec) {
				ctx.log.exec("/usr/bin/ldconfig -p");
			}

			static constinit const char* args[] {"/usr/bin/ldconfig", "-p", nullptr};
			auto output = util::forkExecStdCapture(args, {.requireStatus0 = true, .captureStdOut = true, .captureStdErr = false});

			SplitMutableString lines(output.stdOut);
			auto it = lines.begin();
			if (it == lines.end()) {
				throw Error(FILE_LINE "Output of `ldconfig -p` is empty");
			}

			auto throwParseError = [&](int line, const char* msgSuffix = "") {
				throw Error(
					"%s%d: `ldconfig -p` line %d: could not parse%s%s",
					FILE, line, it.getPartNo(), (msgSuffix != nullptr && msgSuffix[0] != '\0' ? ": " : ""), msgSuffix
				);
			};

			static_assert(std::numeric_limits<int>::max() > 999999999);

			// github issue #2 fix: text is localized
//			std::regex rFirstLine("^(\\d{1,9}) libs found in cache `/etc/ld.so.cache'$");
			std::regex rFirstLine("^(\\d{1,9}) .*$");

			std::cmatch m;
			if (!util::regex_match(*it, m, rFirstLine)) {
				throwParseError(__LINE__);
			}
			int numLibs = std::stoi(m[1]);
			int numAdded = 0;
			int numSkipped = 0;
			data.ldCache.reserve(numLibs);

			// For some libraries, `ldconfig -p` shows '(ELF)' instead of '(libc6[,x86-64])' even though `file {path}` correctly detects 32/64 bit.
			// So I'm parsing type from `ldcontig -p` output, instead will use info from ELFInspector to detect bitness.
			std::regex rLine("^\t(\\S+) \\([^\\)]+\\) => /(\\S+)$");
			for (++it;  it != lines.end();  ++it) {
				if (util::regex_match(*it, m, rLine)) {
					std::string name = m[1];
					// `man 8 ldconfig`: "ldconfig will only look at files that are named lib*.so* (for regular shared objects) or ld-*.so* (for the dynamic loader itself)"
					// I don't need "dynamic loader ifself". But ld-linux.so.2 and ld-linux-x86-64.so.2 are statically linked, so they will be skipped by ELFInspector anyway.
//					if (name.starts_with("ld-")) {
//						numIgnored++;
//						continue;
//					}

					std::string path1 = m[2];
					// Same scope as `path1`, because `path1` maybe reassigned to this buffer.
					char path0Buf[PATH_MAX];

					File* f = nullptr;
					auto it2 = allFilesByPath1.find(path1);
					if (it2 != allFilesByPath1.end()) {
						f = it2->second;
					} else {
						// Maybe (it2 == end()) because `path1` is not realpath?
						// `path` is last on line, and SplitMutableString replaces '\n' with '\0', so `path` will be null-terminated.
						if (!util::realPath(path1.c_str(), path0Buf)) {
							if (ctx.verbosity >= Verbosity_WarnAndExec) {
								ctx.log.warn(FILE_LINE "`ldconfig -p` line %d: skip `/%s`: orphan symlink", it.getPartNo(), path1.c_str());
							}
							numSkipped++;
							continue;
						}
						if (strcmp(path0Buf + 1, path1.c_str())) {
							// This is ok: ldcache maps names to both libs and lib symlinks.
							if (ctx.verbosity >= Verbosity_Debug) {
								ctx.log.debug(FILE_LINE "`ldconfig -p` line %d: rewritten `/%s` ---> `%s`", it.getPartNo(), path1.c_str(), path0Buf);
							}
							path1 = path0Buf + 1;
							it2 = allFilesByPath1.find(path1);
							if (it2 != allFilesByPath1.end()) {
								f = it2->second;
							}
						}
					}
					if (f == nullptr) {
						auto st = util::statx(path1.c_str());
						if (!S_ISREG(st.mode)) {
							if (ctx.verbosity >= Verbosity_WarnAndExec) {
								ctx.log.warn(FILE_LINE "`ldconfig -p` line %d: skip `/%s`: not a regular file", it.getPartNo(), path1.c_str());
							}
							numSkipped++;
							continue;
						}
						f = processRegularFileAfterStatx(path1.c_str(), 0, path1.length(), st.mode, "found in `ldconfig -p`");
					}

					auto inserted = data.ldCache.insert({{alloc::String{ctx.mm, name}, f->is32}, f});
					if (!inserted.second) {
						if (ctx.verbosity >= Verbosity_Debug) {
							if (inserted.first->second == f) {
								// Allow 100% duplicate (both key and value):
								// I got duplicate {`ld-linux.so.2`, 32-bit}` ---> `/usr/lib32/ld-2.33.so` here
								// because both /usr/lib/ld-linux.so.2 and /usr/lib32/ld-linux.so.2 are symlinks to /usr/lib32/ld-2.33.so
								// (while /usr/lib/ld-linux-x86-64.so.2 is symlink to /usr/lib/ld-2.33.so)
								// and `ldconfig -p` output contains two lines:
								//     ld-linux.so.2 (ELF) => /usr/lib32/ld-linux.so.2
								//     ld-linux.so.2 (ELF) => /usr/lib/ld-linux.so.2
								ctx.log.debug(
									FILE_LINE "`ldconfig -p` line %d: skip {`%s`, %s-bit} ---> `/%s`: duplicate key and value",
									it.getPartNo(), name.c_str(), (f->is32 ? "32" : "64"), f->path1.cp()
								);
							} else {
								// BUGFIX (github #1):
								//     If ld.so.cache contains duplicated keys (e.g. libOpenCL.so if /opt/cuda/ is installed)
								//     then looks like `ldd` takes first found row in cache (in the same order as `ldconfig -p` outputs),
								//     so will I.
								ctx.log.warn(
									FILE_LINE "`ldconfig -p` line %d: skip {`%s`, %s-bit} ---> `/%s`: duplicate key, keeping prev value `/%s`",
									it.getPartNo(), name.c_str(), (f->is32 ? "32" : "64"), f->path1.cp(), inserted.first->second->path1.cp()
								);
							}
						}
						numSkipped++;
						continue;
					}

					numAdded++;
					if (ctx.verbosity >= Verbosity_Debug) {
						ctx.log.debug(
							FILE_LINE "`ldconfig -p` line %d: add {`%s`, %s-bit}` ---> `/%s`",
							it.getPartNo(), name.c_str(), (f->is32 ? "32" : "64"), f->path1.cp()
						);
					}
					continue;
				} // if (regex_match(..., rLine))

				// github issue #2 fix: text is localized
//				std::regex rLastLine("^Cache generated by: ldconfig \\(.*$");
				std::regex rLastLine("^\\S.* ldconfig .*$");

				if (!util::regex_match(*it, rLastLine) || ++it != lines.end()) {
					throwParseError(__LINE__);
				}
				break;
			} // for (...;  it != lines.end();  ...)

			// Sanity check.
			if (numAdded + numSkipped != numLibs) {
				throw Error(
					FILE_LINE "output of `ldconfig -p` contains numLibs=%d in first line, but I ended up with numAdded=%d + numSkipped=%d",
					numLibs, numAdded, numSkipped
				);
			}
		} // Call `ldconfig -p`, ...


		// Process queue again, in case files added by `ldconfig -p` filled it.
		// --------------------------------------------------------------------
		processQueue();


		// Fill data.libs.
		// ---------------
		for (auto [path1, f] : allFilesByPath1) {
			if (!f->isLib) {
				continue;
			}
			if (!data.libs.insert({PathAndBitnessKey{.path1 = path1, .is32 = f->is32}, f}).second) {
				throw Error(FILE_LINE "error adding lib {`%s`, %s-bit}: duplicate key", path1.cp(), (f->is32 ? "32" : "64"));
			} else if (ctx.verbosity >= Verbosity_Debug) {
				ctx.log.debug(FILE_LINE "add lib {`%s`, %s-bit} ---> `%s`", path1.cp(), (f->is32 ? "32" : "64"), f->path1.cp());
			}
		}


		if (ctx.verbosity >= Verbosity_Debug) {
			ctx.log.debug(FILE_LINE "stats: processedDirs.size() = %lu", ulong{processedDirs.size()});
			ctx.log.debug(FILE_LINE "stats: allFilesByPath1.size() = %lu", ulong{allFilesByPath1.size()});
			ctx.log.debug(FILE_LINE "stats: data.uniqueFilesByPath1.size() = %lu", ulong{data.uniqueFilesByPath1.size()});
			ctx.log.debug(FILE_LINE "stats: data.libs.size() = %lu", ulong{data.libs.size()});
			ctx.log.debug(FILE_LINE "stats: data.ldCache.size() = %lu", ulong{data.ldCache.size()});
		}
		processedDirs.clear();
		allFilesByPath1.clear();
	}
}
