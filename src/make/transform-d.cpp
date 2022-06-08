// Called by makefile.
// Adds .d file as dependency to .d file, so .d file is regenerated when some include file is edited.
// Preserves mtime.

#include <fstream>
#include <iostream>
#include <regex>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>


int main(int argc, char* argv[]) {
	try {
		if (argc != 2) {
			throw std::runtime_error("Bad args: missing .d-file name");
		}
		std::string name = argv[1];
		if (!std::regex_match(name, std::regex("^target/.+\\.d$"))) {
			throw std::runtime_error("Bad args: invalid .d-file name");
		}

		// Query mtime (maybe even with nanoseconds).
		struct timespec mtime;
		size_t size;
		{
			struct stat s;
			if (stat(name.c_str(), &s) != 0) {
				throw std::runtime_error("stat() failed on `" + name + "`: " + strerror(errno));
			}
			mtime = s.st_mtim;
			size = s.st_size;
		}

		// Read file.
		std::string contents;
		contents.reserve(size);
		{
			std::ifstream is(name);
			contents = std::string((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
		}

		// Write file.
		{
			std::ofstream os(name);
			os << name << ' ' << contents;
		}

		// Restore mtime (with microseconds - 1).
		{
			struct timeval mtime2[2];
			mtime2[0].tv_sec = mtime.tv_sec;
			mtime2[0].tv_usec = (mtime.tv_nsec - 999) / 1000;
			mtime2[1] = mtime2[0];
			if (utimes(name.c_str(), mtime2) != 0) {
				throw std::runtime_error("utimes() failed on `" + name + "`: " + strerror(errno));
			}
		}

		return 0;
	} catch (std::exception& e) {
		std::cerr << argv[0] << ": ERROR: " << e.what() << std::endl;
		return 1;
	}
}
