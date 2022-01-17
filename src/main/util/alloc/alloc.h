#pragma once

#include <stdlib.h>


namespace dimgel::alloc {

	// Declared outside class Arena, otherwise I got "undefined function cannot be used in a constant expression"
	// because inside class (as a static member) it's inaccessible until class is completely parsed: https://stackoverflow.com/a/29662526
	constexpr size_t alignUp(size_t offset, size_t align) noexcept {
		return (offset + align - 1) / align * align;
	}

	// Param `align`: 0 == calculate.
	// Returns new `align` value.
	size_t alignAdjust(size_t size, size_t align);
}
