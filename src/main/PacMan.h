#pragma once

#include <memory>
#include <optional>
#include "data.h"
#include "util/ThreadPool.h"


namespace dimgel {
	class ELFInspector;

	class PacMan {
	protected:
		Context& ctx;
		Data& data;
		ELFInspector& elfInspector;

		// Sanity check: "-\\d" in package dirName is beginning of package version.
		// UPD: There's a package named "qt6-5compat". I've had it. Should not be THAT paranoid anyway.
//		std::regex rPackageName {"^[A-Za-z_][^\\-]*(-[^\\-0-9][^\\-]*)*$"};
//		std::regex rPackageVersion {"^\\d.*$"};

	protected:

		class ParseArchiveTask : public ThreadPool::Task {
			PacMan& owner;
			Package* p = nullptr;

			// 1. onSymlink() collects all symlinks (map key = symlink path, map value = symlink value);
			alloc::StringHashMap<alloc::String> resolvedPath1BySymlinkPath1;

			//    ...and adds those which (name or path0) are in data.unresolvedNeededLibsByName.
			alloc::StringHashSet neededSymlinks;

			// 2. onSymlinksDone() computes final realpaths for all needed symlinks.
			alloc::StringHashMap<alloc::StringHashSet> neededSymlinksByFilePath1;

			// 3. onFileContents() adds regular file to libs if it's needed or contained in neededSymlinksByFilePath1.
			//    This then is merge()d into data.libs for Resolver re-run.
			std::unordered_map<PathAndBitnessKey, File*> libs;

			bool onFileIsNeeded_impl(StringRef filePath1);

		protected:
			alloc::String optDepName;
			alloc::String archiveName;

			// Callbacks for impl().
			void onSymlink(const char* symlinkPath1, const char* resolvedPath);
			void onSymlinksDone();
			bool onFileIsNeeded(const char* filePath1);
			// File contents can be binary (it's unpacked ELF file), so not using BufAndRef: its member StringRef hints for text data.
			void onFileContents(const char* filePath1, char* buf, size_t size);

			// Archive must be scanned twice:
			// 1. `onSymlink()` callback must be called for all symlinks; finally, `onSymlinksDone()` must be called.
			// 2. `if (onFileIsNeeded()) onFileContents(decompresedData)` for all regular files.
			// Also, p.name, p.version and p.provides must be filled.
			virtual void impl(Package* p) = 0;

		public:
			ParseArchiveTask(PacMan& owner, alloc::String optDepName,alloc::String archiveName)
				: owner(owner), optDepName(optDepName), archiveName(archiveName) {}

			void compute() final override;
			void merge() final override;
		};


		// Param `installedPackageUniqueID` is opaque value for base class.
		virtual void iterateInstalledPackages(std::function<void(std::string installedPackageUniqueID)> f) = 0;

		struct parseInstalledPackage_Result {
			Package* p;
			alloc::StringHashSet filePaths1;
		};

		// Called in parallel.
		virtual parseInstalledPackage_Result parseInstalledPackage(const std::string& installedPackageUniqueID) = 0;

		virtual void downloadOptionalDependencies_impl() = 0;

		virtual std::unique_ptr<ParseArchiveTask> createParseArchiveTask(alloc::String optDepName, alloc::String archiveName) = 0;

	public:
		PacMan(Context& ctx, Data& data, ELFInspector& elfInspector) : ctx(ctx), data(data), elfInspector(elfInspector) {}
		virtual ~PacMan() {}

		void parseInstalledPackages();
		void calculateOptionalDependencies();
		void downloadOptionalDependencies();
		void processOptionalDependencies();
	};


	// Implementation-specific, defined in PacMan_*.hpp.
	std::unique_ptr<PacMan> createPacMan(Context& ctx, Data& files, ELFInspector& elfInspector);
}
