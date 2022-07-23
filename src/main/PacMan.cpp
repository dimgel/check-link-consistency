#include "util/Abort.h"
#include "util/Error.h"
#include "util/util.h"
#include STRINGIZE(CONCAT2(PacMan_, DISTRO.hpp))

#undef FILE_LINE
#define FILE_LINE "PacMan:" LINE ": "


namespace dimgel {

	void PacMan::parseInstalledPackages() {
		if (ctx.verbosity >= Verbosity_Default) {
			ctx.log.info("Analyzing installed packages...");
		}

		class Task : public ThreadPool::Task {
			PacMan& owner;
			std::string installedPackageUniqueID;
			parseInstalledPackage_Result result;

		public:
			// Parameter packageDirName will be initialized with temporary value that won't live for the lifetime of this Task; so it's passed by value.
			// Othere parameters are OK to be passed by reference: they all are in scope while tasks work, i.e. until threadPool.waitAll() is called.
			Task(PacMan& owner, std::string installedPackageUniqueID)
				: owner(owner), installedPackageUniqueID(std::move(installedPackageUniqueID))
			{
			}

			// Throws if anything's wrong -- which means some package file is malformed, and there's no much point in proceeding.
			void compute() override {
				result = owner.parseInstalledPackage(installedPackageUniqueID);
			}


			void merge() override {
				if (result.p->name.empty()) {
					throw Error(FILE_LINE "read `%s`: empty package name", ConstCharPtr{installedPackageUniqueID.c_str()});
				}
				if (result.p->version.empty()) {
					throw Error(FILE_LINE "read `%s`: empty package version", ConstCharPtr{installedPackageUniqueID.c_str()});
				}
//				if (!std::regex_match(result.p->name, rPackageName)) {
//					throw Error(FILE_LINE "read `%s`: invalid package name", {installedPackageUniqueID.c_str()});
//				}
//				if (!std::regex_match(result.p->version, rPackageVersion)) {
//					throw Error(FILE_LINE "read `%s`: invalid package version", ConstCharPtr{installedPackageUniqueID.c_str()});
//				}

				if (owner.ctx.verbosity >= Verbosity_Debug) {
					owner.ctx.log.debug(
						FILE_LINE "read `%s`: package `%s %s`",
						ConstCharPtr{installedPackageUniqueID.c_str()}, ConstCharPtr{result.p->name}, ConstCharPtr{result.p->version}
					);
					for (auto& s : result.p->provides) {
						owner.ctx.log.debug(FILE_LINE "read `%s`: provides `%s`", ConstCharPtr{installedPackageUniqueID.c_str()}, ConstCharPtr{s});
					}
					for (auto& s : result.p->optDepends) {
						owner.ctx.log.debug(FILE_LINE "read `%s`: optDepends `%s`", ConstCharPtr{installedPackageUniqueID.c_str()}, ConstCharPtr{s});
					}
				}

				if (auto t2 = owner.data.packagesByName.insert({result.p->name, result.p});  !t2.second) {
					auto p0 = t2.first->second;
					throw Error(
						FILE_LINE "read `%s`: another installed package has same name: `%s %s`",
						ConstCharPtr{installedPackageUniqueID.c_str()}, ConstCharPtr{p0->name}, ConstCharPtr{p0->version}
					);
				}

				auto insertByProvides = [&](alloc::String s) {
					if (auto t2 = owner.data.packagesByProvides.insert({s, result.p});  !t2.second && owner.ctx.verbosity >= Verbosity_Debug) {
						// This is legal: e.g. `nvidia-utils` & `vulkan-intel` both provide `vulkan-driver`; currently there are 10 such "conflicts" on my system.
						// And I actually don't care who provides dependency; I'll only download dependencies provided by nobody.
						auto p0 = t2.first->second;
						owner.ctx.log.debug(
							FILE_LINE "read `%s`: another installed package already provides `%s`: `%s %s`",
							ConstCharPtr{installedPackageUniqueID.c_str()}, ConstCharPtr{s}, ConstCharPtr{p0->name}, ConstCharPtr{p0->version}
						);
					}
				};
				insertByProvides(result.p->name);
				for (auto& s : result.p->provides) {
					insertByProvides(s);
				}

				for (auto s : result.filePaths1) {
					if (auto t2 = owner.data.packagesByFilePath1.insert({s, result.p});  !t2.second) {
						// This was a warning once, but from pacman's point of view it's error.
						// Don't know if `pacman -Qkk` checks for it, won't be bad to fail-fast here anyway.
						auto p0 = t2.first->second;
						throw Error(
							FILE_LINE "read `%s`: another installed package already owns file `%s`: `%s %s`",
							ConstCharPtr{installedPackageUniqueID.c_str()}, ConstCharPtr{s}, ConstCharPtr{p0->name}, ConstCharPtr{p0->version}
						);
					}
					if (owner.ctx.verbosity >= Verbosity_Debug) {
						owner.ctx.log.debug(FILE_LINE "read `%s`: owns file `%s`", ConstCharPtr{installedPackageUniqueID.c_str()}, ConstCharPtr{s});
					}
				}
			}
		}; // class Task


		data.packagesByName.reserve(1500);
		data.packagesByProvides.reserve(2500);
		data.packagesByFilePath1.reserve(370000);

		std::vector<std::unique_ptr<ThreadPool::Task>> tasks;
		tasks.reserve(1500);
		iterateInstalledPackages([&](std::string installedPackageUniqueID) {
			tasks.push_back(std::make_unique<Task>(*this, std::move(installedPackageUniqueID)));
		});
		ctx.threadPool.addTasks(ctx.threadPool.groupTasks(std::move(tasks)));
		ctx.threadPool.waitAll();

		if (ctx.verbosity >= Verbosity_Debug) {
			ctx.log.debug(FILE_LINE "stats: data.packagesByName.size() = %lu", ulong{data.packagesByName.size()});
			ctx.log.debug(FILE_LINE "stats: data.packagesByProvides.size() = %lu", ulong{data.packagesByProvides.size()});
			ctx.log.debug(FILE_LINE "stats: data.packagesByFilePath1.size() = %lu", ulong{data.packagesByFilePath1.size()});
		}
	} // PacMan::parseInstalledPackages()


	//----------------------------------------------------------------------------------------------------------------------------------------


	void PacMan::calculateOptionalDependencies() {
		for (auto& [_, f] : data.uniqueFilesByPath1) {
			if (f->belongsToPackage == nullptr) {
				continue;
			}
			for (auto& optdep : f->belongsToPackage->optDepends) {
				if (data.packagesByProvides.contains(optdep)) {
					// We're not interested in already installed optional dependencies.
					continue;
				}
				if (data.archiveNamesByOptDepend.insert({optdep, {}}).second && ctx.verbosity >= Verbosity_Debug) {
					ctx.log.debug(
						FILE_LINE "add optional dependency `%s` for package `%s %s`",
						ConstCharPtr{optdep}, ConstCharPtr{f->belongsToPackage->name}, ConstCharPtr{f->belongsToPackage->version}
					);
				}
			}
		}

		data.optDependsSorted.reserve(data.archiveNamesByOptDepend.size());
		for (auto& [od, _] : data.archiveNamesByOptDepend) {
			data.optDependsSorted.push_back(od);
		}
		util::sort(data.optDependsSorted);
	}


	//----------------------------------------------------------------------------------------------------------------------------------------


	// data.unresolvedNeededLibsByName contains either name not containing '/', or absolute path starting with '/'.
	// Both ways, I got exhaustive list of what I'm looking for, so I don't need to check for "*.so*" name pattern here.
	bool PacMan::ParseArchiveTask::onFileIsNeeded_impl(StringRef filePath1) {
		auto fileName = filePath1;
		auto lastSlash = fileName.rfind('/');
		if (lastSlash != fileName.npos) {
			fileName.remove_prefix(lastSlash + 1);
		}
		if (owner.data.unresolvedNeededLibNames.contains(fileName)) {
			return true;
		}

		char filePath0[PATH_MAX];
		filePath0[0] = '/';
		strcpy(filePath0 + 1, filePath1.cp());
		if (owner.data.unresolvedNeededLibNames.contains(filePath0)) {
			return true;
		}

		return false;
	};


	//----------------------------------------------------------------------------------------------------------------------------------------


	void PacMan::ParseArchiveTask::onSymlink(const char* symlinkPath1, const char* resolvedPath) {
		if (resolvedPath == nullptr || resolvedPath[0] == '\0') {
			return;
		}

		if (onFileIsNeeded_impl(StringRef{symlinkPath1})) {
			neededSymlinks.insert(alloc::String{owner.ctx.mm, symlinkPath1});
			if (owner.ctx.verbosity >= Verbosity_Debug) {
				owner.ctx.log.debug(FILE_LINE "read `%s`: symlink.addNeeded: `/%s`", ConstCharPtr{archiveName}, ConstCharPtr{symlinkPath1});
			}
		}

		char resolvedPath1Buf[PATH_MAX];
		StringRef resolvedPath1;
		if (resolvedPath[0] == '/') {
			resolvedPath1 = resolvedPath + 1;
		} else {
			resolvedPath1 = util::concatStringViews(resolvedPath1Buf, sizeof(resolvedPath1Buf), {symlinkPath1, "/../", resolvedPath});
		}

		char normalizedPath1Buf[PATH_MAX];
		auto normalizedPath1Length = util::normalizePath(resolvedPath1.cp(), normalizedPath1Buf);
		alloc::String normalizedPath1{owner.ctx.mm, normalizedPath1Buf, normalizedPath1Length};

		if (owner.ctx.verbosity >= Verbosity_Debug) {
			// normalizedPath1 may be path to another symlink. Don't resolve to regular file until all symlinks are obtained; see onSymlinksDone().
			owner.ctx.log.debug(
				FILE_LINE "read `%s`: symlink.add: `/%s` ---> `%s` ---> `/%s`",
				ConstCharPtr{archiveName}, ConstCharPtr{symlinkPath1}, ConstCharPtr{resolvedPath}, ConstCharPtr{normalizedPath1}
			);
		}
		// Store symlink even if it's not needed itself: some needed (including yet unmet) symlink may resolve to it, so I might need it in onSymlinksDone().
		resolvedPath1BySymlinkPath1.insert({alloc::String{owner.ctx.mm, symlinkPath1}, normalizedPath1});
	};


	//----------------------------------------------------------------------------------------------------------------------------------------


	void PacMan::ParseArchiveTask::onSymlinksDone() {
		for (auto& symlinkPath1 : neededSymlinks) {
			// Or it maybe not a regular file (e.g. directory), but that will be decided later: if onFileIsNeeded() wasn't called for resolved regular file,
			// it would mean there's no such file, so symlink will be thrown away as pointing to non-existent or non-regular file.
			auto regularPath1 = symlinkPath1;
			while (true) {
				auto it = resolvedPath1BySymlinkPath1.find(regularPath1);
				if (it == resolvedPath1BySymlinkPath1.end()) {
					break;
				}
				regularPath1 = it->second;
			}

			if (owner.ctx.verbosity >= Verbosity_Debug) {
				owner.ctx.log.debug(
					FILE_LINE "read `%s`: symlink.neededToFile: `/%s` ---> `/%s`",
					ConstCharPtr{archiveName}, ConstCharPtr{symlinkPath1}, ConstCharPtr{regularPath1}
				);
			}
			neededSymlinksByFilePath1[regularPath1].insert(symlinkPath1);
		}

		resolvedPath1BySymlinkPath1.clear();
		neededSymlinks.clear();
	}


	//----------------------------------------------------------------------------------------------------------------------------------------


	bool PacMan::ParseArchiveTask::onFileIsNeeded(const char* filePath1) {
		StringRef sr(filePath1);
		return neededSymlinksByFilePath1.contains(sr) || onFileIsNeeded_impl(sr);
	}


	//----------------------------------------------------------------------------------------------------------------------------------------


	void PacMan::ParseArchiveTask::onFileContents(const char* filePath1, char* buf, size_t size) {
		if (owner.ctx.verbosity >= Verbosity_Debug) {
			owner.ctx.log.debug(FILE_LINE "read `%s`: neededFile.inspect `/%s`", ConstCharPtr{archiveName}, ConstCharPtr{filePath1});
		}

		File* f = File::create(owner.ctx.mm);
		f->path1 = alloc::String{owner.ctx.mm, filePath1};   // elfInspector uses this to show messages.
		owner.elfInspector.processOne_fromArchive(*f, buf, size);
		if (!f->isLib) {
			if (owner.ctx.verbosity >= Verbosity_WarnAndExec) {
				owner.ctx.log.warn(FILE_LINE "read `%s`: neededFile.notLibrary `/%s`", ConstCharPtr{archiveName}, ConstCharPtr{filePath1});
			}
			return;
		}

		auto addLib = [&](alloc::String path1, File* f) {
			if (libs.insert({PathAndBitnessKey{.path1 = path1, .is32 = f->is32}, f}).second) {
				if (owner.ctx.verbosity >= Verbosity_Debug) {
					owner.ctx.log.debug(
						FILE_LINE "read `%s`: libs.add {`%s`, %s-bit} ---> `%s`",
						ConstCharPtr{archiveName}, ConstCharPtr{path1}, (f->is32 ? "32" : "64"), ConstCharPtr{f->path1}
					);
				}
			} else {
				if (owner.ctx.verbosity >= Verbosity_WarnAndExec) {
					owner.ctx.log.warn(
						FILE_LINE "read `%s`: libs.error {`%s`, %s-bit}: duplicate key, ignoring",
						ConstCharPtr{archiveName}, ConstCharPtr{path1}, (f->is32 ? "32" : "64")
					);
				}
			}
		};
		addLib(f->path1, f);
		auto it = neededSymlinksByFilePath1.find(f->path1);
		if (it != neededSymlinksByFilePath1.end()) {
			for (auto symlinkPath1 : it->second) {
				addLib(symlinkPath1, f);
			}
		}
	};


	//----------------------------------------------------------------------------------------------------------------------------------------


	void PacMan::ParseArchiveTask::compute() {
		p = Package::create(owner.ctx.mm);
		p->archiveName = archiveName;

		try {
			impl(p);
			if (p->name.empty()) {
				if (owner.ctx.verbosity >= Verbosity_WarnAndExec) {
					owner.ctx.log.warn(FILE_LINE "ignore `%s`: empty package name", ConstCharPtr{archiveName});
				}
				throw Abort();
			}
			if (p->version.empty()) {
				if (owner.ctx.verbosity >= Verbosity_WarnAndExec) {
					owner.ctx.log.error(FILE_LINE "ignore `%s`: empty package version", ConstCharPtr{archiveName});
				}
				throw Abort();
			}

			// Maybe "optdepend=java-runtime", but "provides=java-runtime=17". And not only "="; maybe "depend>=...", etc.
			// So just debug message. Warnings were already given when I was exec()ing `pacman -Swp`.
			if (p->name != optDepName && !p->provides.contains(optDepName)) {
				if (owner.ctx.verbosity >= Verbosity_Debug) {
					owner.ctx.log.debug(FILE_LINE "read `%s`: neither package name nor any of its `provides` entries match optDependName `%s`",
						ConstCharPtr{archiveName}, ConstCharPtr{optDepName}
					);
				}
			}

			if (owner.ctx.verbosity >= Verbosity_Debug) {
				owner.ctx.log.debug(FILE_LINE "read `%s`: package `%s %s`", ConstCharPtr{archiveName}, ConstCharPtr{p->name}, ConstCharPtr{p->version});
				for (auto& s : p->provides) {
					owner.ctx.log.debug(FILE_LINE "read `%s`: provides `%s`", ConstCharPtr{archiveName}, ConstCharPtr{s});
				}
			}
		} catch (Abort& e) {
			p = nullptr;
		} catch (std::exception& e) {
			if (owner.ctx.verbosity >= Verbosity_WarnAndExec) {
				owner.ctx.log.warn(FILE_LINE "ignore `%s`: %s", ConstCharPtr{optDepName}, ConstCharPtr{e.what()});
			}
			p = nullptr;
		}
	}


	//----------------------------------------------------------------------------------------------------------------------------------------


	void PacMan::ParseArchiveTask::merge() {
		if (p == nullptr) {
			// compute() failed.
			return;
		}

		for (auto [pathAndBitness, f] : libs) {
			if (!owner.data.libs.insert({pathAndBitness, f}).second && owner.ctx.verbosity >= Verbosity_WarnAndExec) {
				owner.ctx.log.warn(
					FILE_LINE "read `%s`: libs.error {`%s`, %s-bit}: duplicate key, ignoring",
					ConstCharPtr{archiveName}, ConstCharPtr{f->path1}, (f->is32 ? "32" : "64")
				);
			}
		}

		neededSymlinksByFilePath1.clear();
		libs.clear();
	}


	//----------------------------------------------------------------------------------------------------------------------------------------


	void PacMan::downloadOptionalDependencies() {
		if (ctx.verbosity >= Verbosity_Default) {
			ctx.log.info("%s optional dependencies of problematic packages...", (ctx.noNetwork ? "Locating" : "Downloading"));
		}
		downloadOptionalDependencies_impl();
	}


	//----------------------------------------------------------------------------------------------------------------------------------------


	// TODO !!! Why does not work in parallel? Maybe need some wrapper around libarchive?
	void PacMan::processOptionalDependencies() {
		if (ctx.verbosity >= Verbosity_Default) {
			ctx.log.info("Analyzing optional dependencies of problematic packages...");
		}

		std::vector<std::unique_ptr<ThreadPool::Task>> tasks;
		tasks.reserve(data.archiveNamesByOptDepend.size());
		for (auto& [optdep, archiveName] : data.archiveNamesByOptDepend) {
			if (!archiveName.empty()) {
				tasks.push_back(createParseArchiveTask(optdep, archiveName));
			}
		};
		ctx.threadPool.addTasks(ctx.threadPool.groupTasks(std::move(tasks)));
		ctx.threadPool.waitAll();
	}
}
