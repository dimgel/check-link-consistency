#include <assert.h>
#include <mutex>
#include "../util.h"
#include "alloc.h"

#define FILE_LINE "alloc:" LINE ": "


namespace dimgel::alloc {

	size_t alignAdjust(size_t size, size_t align) {
		static constinit uint8_t aligns[8] = {8, 1, 2, 1, 4, 1, 2, 1};
		if (size == 0) {
			throw std::runtime_error(FILE_LINE "alignAutodetect(): size == 0");
		}
		if (align == 0) {
			return aligns[size & 0x7];
		}
		if ((size % align) != 0) {
			// Sanity check.
			throw std::runtime_error(FILE_LINE "alignAutodetect(): (size % align) != 0");
		}
		return align;
	}
}
