#pragma once

#include <dirent.h>
#include <optional>
#include <regex>
#include <sys/stat.h>
#include <vector>
#include "alloc/String.h"
#include "BufAndRef.h"


// http://stackoverflow.com/a/1489985
#define CONCAT2_(a,b) a ## b
#define CONCAT2(a,b) CONCAT2_(a,b)
// https://stackoverflow.com/a/33362845
#define STRINGIZE_(a) #a
#define STRINGIZE(a) STRINGIZE_(a)

#define LINE STRINGIZE(__LINE__)


namespace dimgel::util {

	// std::initializer_list PROBABLY does not use heap memory: https://stackoverflow.com/questions/61825532
	StringRef concatStringViews(char* buf, size_t bufSize, std::initializer_list<std::string_view> sources);


	void trimInplace(std::string_view& s);
	inline std::string trim(std::string_view s) { trimInplace(s);  return (std::string)s; }
	inline std::string trim(const std::string& s) { return trim(std::string_view(s)); }


	inline auto regex_match(std::string_view s, std::regex r) { return std::regex_match(s.begin(), s.end(), r); }
	inline auto regex_match(StringRef        s, std::regex r) { return regex_match(s.sv(), r); }
	inline auto regex_match(alloc::String    s, std::regex r) { return regex_match(s.sv(), r); }

	inline auto regex_match(std::string_view s, std::cmatch& m, std::regex r) { return std::regex_match(s.begin(), s.end(), m, r); }
	inline auto regex_match(StringRef        s, std::cmatch& m, std::regex r) { return regex_match(s.sv(), m, r); }
	inline auto regex_match(alloc::String    s, std::cmatch& m, std::regex r) { return regex_match(s.sv(), m, r); }


	template<class T               > void sort(std::vector<T>& v             ) { std::sort(v.begin(), v.end()     ); }
	template<class T, class Compare> void sort(std::vector<T>& v, Compare cmp) { std::sort(v.begin(), v.end(), cmp); }

	template<class T> bool contains(const std::vector<T>& v, const T& x) {
		return std::find(v.begin(), v.end(), x) != v.end();
	}


	// Returns false on ENOENT.
	// Throws on other errors.
	bool realPath(const char* path, char* buf);

	// Acts somewhat like realPath() but does not consult filesystem; i.e. path is not required to exist.
	// Returns strlen(buf).
	// E.g.: "a/b/c/../.././d///e/" ---> "a/d/e/".
	size_t             normalizePath(const char* path, char* buf);
	inline std::string normalizePath(const char* path)               { char buf[PATH_MAX];  normalizePath(path, buf);  return buf; }
	inline std::string normalizePath(const std::string& path)        { return normalizePath(path.c_str()); }


	struct statx_Result {
		decltype(stat::st_mode) mode;
		decltype(stat::st_ino) inode;
	};

	statx_Result statx(const char* path);


	// If path is missing, returns without error.
	// Throws if path is not a directory or symlink to directory, or if callback throws.
	void scanDir(const char* path, std::function<void(const struct dirent&)> callback);


	BufAndRef readFile(const char* path);


	// argv[0] must contain absoulte path, it will be also used for `pathname` parameter of `execv()`.
	// argv[]'s last element must be nullptr.
	// Returns exit status.
	// Throws on errors, if program aborted by signal, or if requireStatus0 == true but program exit status != 0.
	int forkExec(const char* argv[], bool requireStatus0 = true);

	struct forkExecStdCapture_Params {
		bool requireStatus0 = true;
		bool captureStdOut = false;
		bool captureStdErr = false;
	};
	struct forkExecStdCapture_Result {
		int status;
		std::string stdOut;
		std::string stdErr;
	};

	// Same as forkExec() but also captures and returns child's stdOut and/or stdErr.
	forkExecStdCapture_Result forkExecStdCapture(const char* argv[], forkExecStdCapture_Params p);
}
