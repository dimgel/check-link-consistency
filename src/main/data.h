#pragma once

#include <atomic>
#include <dirent.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "util/alloc/MemoryManager.h"
#include "util/alloc/String.h"


namespace dimgel {

	// Tests (ctx.verbosity >= Verbosity_Debug) are most frequent, so defined Verbosity_Debug = 0.
	static constexpr int Verbosity_Quiet = -3;
	static constexpr int Verbosity_Default = -2;
	static constexpr int Verbosity_VeryImportantWarn = Verbosity_Default;
	static constexpr int Verbosity_WarnAndExec = -1;
	static constexpr int Verbosity_Debug = 0;


	//----------------------------------------------------------------------------------------------------------------------------------------


	struct PathAndBitnessKey {
		alloc::String path1;
		bool is32;
		bool operator ==(const PathAndBitnessKey& x) const = default;
	};
}

namespace std {
	// https://en.cppreference.com/w/cpp/utility/hash
	// https://www.cppstories.com/2021/heterogeneous-access-cpp20/
	template<> struct hash<dimgel::PathAndBitnessKey> {
		using is_transparent = void;

		size_t operator() (const dimgel::PathAndBitnessKey& x) const {
			auto h1 = std::hash<std::string_view>{}(x.path1.sv());
			auto h2 = std::hash<bool>{}(x.is32);
			return h1 ^ (h2 << 1);
		}

		// ATTENTION! Must be same as operator()(const PathAndBitnessKey&) above. To search without allocating String.
		size_t operator() (const std::pair<dimgel::StringRef, bool>& x) const {
			auto h1 = std::hash<std::string_view>{}(x.first.sv());
			auto h2 = std::hash<bool>{}(x.second);
			return h1 ^ (h2 << 1);
		}
	};
}

namespace dimgel {
	inline bool operator ==(const PathAndBitnessKey& a, const std::pair<StringRef, bool>& b) {
		return a.path1 == b.first && a.is32 == b.second;
	}

	using PathAndBitnessMap = std::unordered_map<PathAndBitnessKey, class File*, std::hash<PathAndBitnessKey>, std::equal_to<>>;


	//----------------------------------------------------------------------------------------------------------------------------------------


	struct SearchPath {
		alloc::String path1;
		ino_t inode;
		bool operator ==(const SearchPath& x) const {
			return x.inode == inode;
		}
	};

	struct AddOptDepend {
		int configLineNo;
		alloc::String optDepName;
	};

	struct AddLibPath {
		int configLineNo;
		alloc::String path0;
		ino_t inode;
	};

	// Common configs & dependencies for controllers.
	struct Context final {

		// Configuration:

		int verbosity;
		bool wideOutput;
		bool colorize;
		struct Colors& colors;
		bool useOptionalDeps;
		bool noNetwork;

		std::vector<SearchPath>& scanBins;          // defaults_*.hpp/scanDefaultBins + .conf/scanMoreBins
		std::vector<SearchPath>& scanDefaultLibs;   // defaults_*.hpp/scanDefaultLibs
		std::vector<SearchPath>& scanMoreLibs;      // LD_LIBRARY_PATH + .conf/scanMoreLibs
		alloc::StringHashMap<std::vector<AddLibPath>>& addLibPathsByFilePath1Prefix;        // .conf/addLibPath
		std::unordered_map<class Package*, std::vector<AddLibPath>> addLibPathsByPackage;   // .conf/addLibPath


		// Dependencies:

		class Log& log;
		class ThreadPool& threadPool;
		alloc::MemoryManager& mm;   // Keeps data until program terminates.
	};


	//----------------------------------------------------------------------------------------------------------------------------------------


	class File final {
		File() {}

	public:
		static File* create(alloc::MemoryManager& mm) {
			return new(mm) File();
		}

		// Realpath without leading '/' (to match /var/lib/pacman/local/*/files entries).
		alloc::String path1;
		std::vector<SearchPath> configPaths;
		std::vector<SearchPath> rPaths;
		std::vector<SearchPath> runPaths;

		// Name not containing '/', or absolute path starting with '/'.
		alloc::StringHashSet neededLibs;

		class Package* belongsToPackage = nullptr;

		// Has SUID/SGUID attribute? See `man 8 ld.so` / "secure execution".
		bool isSecure;

		std::atomic<bool> isInspected {false};
		static_assert(sizeof(isInspected) == 1);

		// Do we need to process this file at all, or it's non-ELF or statically linked?
		bool isDynamicELF = false;
		bool isLib = false;
		bool is32;
	};


	//----------------------------------------------------------------------------------------------------------------------------------------


	class Package final {
		Package() {}
	public:
		static Package* create(alloc::MemoryManager& mm) {
			return new(mm) Package();
		}

		alloc::String name;      // E.g. "gcc".
		alloc::String version;   // E.g. "11.1.0-1".

		// For downloaded optional dependencies.
		alloc::String archiveName;

		// Package names (e.g. "libeudev") and virtual dependencies (e.g. "libudev", "libudev.so=1-64").
		alloc::StringHashSet provides;
		alloc::StringHashSet optDepends;
	};


	//----------------------------------------------------------------------------------------------------------------------------------------


	struct Data final {
		// Filled by PacMan::parseInstalledPackages().
		// Used to rewrite Context.addLibByPackageName and addLibPathByPackageName to speed up access.
		alloc::StringHashMap<Package*> packagesByName;

		// Filled by PacMan::parseInstalledPackages().
		// Used by PacMan::calculateOptionalDependencies() to filter out already installed dependencies.
		alloc::StringHashMap<Package*> packagesByProvides;

		// Filled by PacMan::parseInstalledPackages().
		// Used by FileCollector to assign File.belongsToPackage.
		// Key = file realpath1 belonging to package. Multiple files may belong to same package.
		alloc::StringHashMap<Package*> packagesByFilePath1;

		// Files to be analyzed by Resolver. Filled by FilesCollector, successfully resolved files are removed by Resolver.
		// Key = canonical file path, key == value->path1. Needed to process (by ELFInspector, Resolver) each file only once.
		alloc::StringHashMap<File*> uniqueFilesByPath1;

		// Searched by Resolver. Filled by FilesCollector and PacMan.
		// Key = canonical or symlink path1. Multiple keys may reference same File.
		PathAndBitnessMap libs;

		// Searched by Resolver. Filled by FilesCollector.
		// Key = .so name, not path.
		PathAndBitnessMap ldCache;

		// Filled by Resolver.
		//
		// PacMan scans downloaded package archives looking for these unresolved libraries; it then unpacks found files to memory,
		// calls ELFInspector on them, and (if they turn out to be libraries) adds them to data.libs for next Resolver run.
		// NOTE: After data.libs is updated, all information about optional dependencies can be forgotten (unless it's needed for descriptive debug output);
		//       Resolver does not care where data.libs elements came from.
		//
		// Key = neededLib name without '/', or absolute path with leading '/' (see how ELFInspector fills File::neededLibs) that was not found on system.
		alloc::StringHashSet unresolvedNeededLibNames;

		// Filled by PacMan::assignProblematicFilesToInstalledPackages().
		// Used by PacMan::downloadOptionalDependencies() and PacMan::processOptionalDependencies().
		// Key = non-installed optional dependency (package name or virtual dependency).
		alloc::StringHashMap<alloc::String> archiveNamesByOptDepend;

		// Keys of optDepends, sorted for readability of generated `pacman -Sw` command.
		std::vector<alloc::String> optDependsSorted;
	};
}
