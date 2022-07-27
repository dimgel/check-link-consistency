#include <archive.h>
#include <archive_entry.h>
#include <fcntl.h>
#include "ArchiveReader.h"
#include "Error.h"
#include "Finally.h"
#include "util.h"

#define FILE_LINE "ArchiveReader:" LINE ": "


namespace dimgel {

	ArchiveReader::ArchiveReader(std::string path_)
		: path(std::move(path_)),
		  fd {::open(path.c_str(), O_RDONLY)}
	{
		if (fd == -1) {
			throw Error(FILE_LINE "`%s`: open() failed: %s", path.c_str(), strerror(errno));
		}
	}


	void ArchiveReader::scanPartial(std::function<bool(archive_entry*)> onEntry) {
		// Otherwise 2nd scan won't work:
		if (alreadyScanned && lseek(fd, 0, SEEK_SET) == -1) {
			throw Error(FILE_LINE "`%s`: lseek() failed: %s", path.c_str(), strerror(errno));
		}
		alreadyScanned = true;

		if (a != nullptr) {
			throw Error(FILE_LINE "`%s`: already inside scan()", path.c_str());
		}
		a = archive_read_new();
		Finally aFin([&]{
			archive_read_free(a);
			a = nullptr;
		});

		archive_read_support_filter_all(a);
		archive_read_support_format_all(a);
		if (archive_read_open_fd(a, fd, 10240) != ARCHIVE_OK) {
			throw Error(FILE_LINE "`%s`: archive_read_open_fd() failed: %s", path.c_str(), archive_error_string(a));
		}

		archive_entry* e;
		while (archive_read_next_header(a, &e) == ARCHIVE_OK) {
			if (!onEntry(e)) {
				break;
			}
		}
	}


	BufAndRef ArchiveReader::getEntryData(archive_entry* e) {
		if (a == nullptr) {
			throw Error(FILE_LINE "`%s`: not inside scan()", path.c_str());
		}
		if (!archive_entry_size_is_set(e)) {
			throw Error(FILE_LINE "`%s` / `%s`: archive_entry_size_is_set() == false", path.c_str(), archive_entry_pathname(e));
		}
		auto ssize = archive_entry_size(e);
		if (ssize < 0) {
			throw Error(FILE_LINE "`%s` / `%s`: archive_entry_size() failed: %s", path.c_str(), archive_entry_pathname(e), archive_error_string(a));
		}

		auto buf = std::make_unique<char[]>(ssize + 1);
		char* s = buf.get();

		auto sizeRead = archive_read_data(a, s, ssize);
		if (sizeRead < 0) {
			throw Error(FILE_LINE "`%s` / `%s`: archive_read_data() failed: %s", path.c_str(), archive_entry_pathname(e), archive_error_string(a));
		}
		if (sizeRead != ssize) {
			throw Error(
				FILE_LINE "`%s` / `%s`: archive_read_data() returned %ld != size %ld",
				path.c_str(), archive_entry_pathname(e), long{sizeRead}, long{ssize}
			);
		}

		// Support for text files (see also [size + 1] in buf allocation).
		s[ssize] = '\0';

		return {.buf {std::move(buf)}, .ref {StringRef::createUnsafe(s, (size_t)ssize)}};
	}
}
