#pragma once

#include <memory>
#include "BufAndRef.h"
#include "Closeable.h"

struct archive;
struct archive_entry;


namespace dimgel {

	// https://github.com/libarchive/libarchive/wiki/Examples
	class ArchiveReader final {
		std::string path;
		Closeable fd;
		archive* a = nullptr;
		bool alreadyScanned = false;

	public:
		ArchiveReader(std::string path_);

		ArchiveReader(const ArchiveReader&) = delete;
		ArchiveReader(ArchiveReader&&) = delete;
		ArchiveReader& operator =(const ArchiveReader&) = delete;
		ArchiveReader& operator =(ArchiveReader&&) = delete;

		// May be called multiple times.
		// Calls onEntry() for each archive entry; if onEntry() returns false, stop iterating.
		void scanPartial(std::function<bool(archive_entry*)> onEntry);

		void scanAll(std::function<void(archive_entry*)> onEntry) {
			scanPartial([&](archive_entry* e) {
				onEntry(e);
				return true;
			});
		}

		BufAndRef getEntryData(archive_entry* e);
	};
}
