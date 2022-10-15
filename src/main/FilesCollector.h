#pragma once

#include <functional>
#include <queue>
#include <regex>
#include <sys/stat.h>
#include <unordered_set>
#include "data.h"
#include "util/Spinlock.h"


namespace dimgel {
	class ELFInspector;


	// Collects bins & libs from system.
	// What is skipped by this class will be invisible for futher processing: won't be analyzed, won't participate in dependency resolution, etc.
	class FilesCollector {

		struct PostponedResolved {
			std::string resolvedPath1;
			decltype(stat::st_mode) st_mode;
		};

		Context& ctx;
		Data& data;
		ELFInspector& elfInspector;

		// Regex to match last path component (fileName) of regular file -- i.e. regName; see regNameOffset parameter of this class' methods.
		// Says `man ldconfig`: "ldconfig will only look at files that are named lib*.so* (for regular shared objects)" ...
		// But I'd rather look for "*.so" and "*.so.*".
		const std::regex rLibName{"^.+\\.so(\\..*)?$"};


		// 1. First we scan filesystem in main thread.
		// 2. Only after scan is completed, we call ELFInspector-s in parallel.
		// 3. ELFInspector-s can request more SearchPath-s to scan -- those it finds in ELFs' DT_RPATH and DT_RUNPATH entries.
		//    Those new SearchPath-s are appended to queue.
		Spinlock queueSpinlock {};
		std::queue<SearchPath> queue;

		// Not only many DT_RUNPATH-s contain /usr/lib which is already scanned by default,
		// but symlinks may point to already scanned directories too: /bin ---> /usr/bin, /lib ---> /usr/lib, etc.
		std::unordered_set<ino_t> processedDirs;

		// Which files to run ELFInspector-s on.
		std::vector<File*> uniqueFilesAddedByCurrentIteration;

		// Only after ELFInspector-s are completed, we know which files are 32-bit / 64-bit / non-ELFs, and can fill `libs`.
		// Until then, here we collect all files [to be] processed by ELFInspector-s.
		// Key = canonical or symlink path. Multiple keys may reference same File. Used to fill `libs` and `ldCache`.
		alloc::StringHashMap<File*> allFilesByPath1;

		// Code deduplication. If `reason` != nullptr, then file is added unconditionally; otherwise its x-permission and extension are checked first.
		// Param `regNameOffset` is needed if `reason` == nullptr.
		// Param `st_mode` is always needed.
		File* processRegularFileAfterStatx(const char* path1, size_t regNameOffset, size_t length, decltype(stat::st_mode) st_mode, const char* reason = nullptr);

		// Param `path1` is char[PATH_MAX] without leading '/', `length` is current size to append to; path1[length] must be '\0'.
		// Must be absolute, otherwise:
		// - `readlink(2)` might behave strangely (didn't test thoroughly, but they say);
		// - `realpath(3)` (which I use) will try to resolve path relative to current dir.   // TODO Wtf? I already call chdir("/") in main.
		// Must be realpath (no symlinks, etc.), see "On symlinks" in notes/decisions.txt.
		//
		// Param `regNameOffset` is offset of last name component; needed only for d_type == DT_REG.
		// Param `dirInode` is needed only for d_type == DT_DIR.
		void processRecursive(char* path1, size_t regNameOffset, size_t length, ino_t dirInode, uint8_t d_type);

		void processQueue();

		// Called from ELFInspector::Task::compute() with File.rPath and File.runPath entries.
		void scanAdditionalDir(SearchPath searchPath) {
			std::lock_guard g(queueSpinlock);
			queue.push(searchPath);
		}

	public:
		FilesCollector(Context& ctx, Data& data, ELFInspector& elfInspector) : ctx(ctx), data(data), elfInspector(elfInspector) {}

		// Must be called only once.
		void execute();
	};
}
