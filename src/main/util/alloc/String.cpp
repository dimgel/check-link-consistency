#include "../Error.h"
#include "../util.h"
#include "MemoryManager.h"
#include "String.h"

#define FILE_LINE "String:" LINE ": "


namespace dimgel::alloc {

	// Param `length` does NOT include terminator '\0', so (length + 1) bytes are allocated.
	static char* allocate(MemoryManager& mm, size_t length) {
		return reinterpret_cast<char*>(mm.allocate(length + 1, 1));
	}


	String::String(MemoryManager& mm, std::string_view source) {
		auto n = source.length();
		char* buf = allocate(mm, n);
		memcpy(buf, source.data(), n);
		buf[n] = '\0';
		x = StringRef::createUnsafe(buf, n);
	}


	String::String(MemoryManager& mm, std::initializer_list<std::string_view> sources) {
		size_t n = 0;
		for (auto& src : sources) {
			n += src.length();
		}
		char* buf = allocate(mm, n);
		x = util::concatStringViews(buf, n + 1, std::move(sources));
		if (x.length() != n) {
			throw std::runtime_error(FILE_LINE "concatStrings(): internal error: result length mismatch");
		}
	}
}
