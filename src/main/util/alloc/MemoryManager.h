#pragma once

#include <stdlib.h>

namespace dimgel {
	class Log;
}


namespace dimgel::alloc {

	class MemoryManager {
	public:
		// Thread-safe:
		virtual void* allocate(size_t size, size_t align) = 0;
		virtual void free(void* p, size_t size) = 0;

		// NOT thread-safe:
		virtual void reset() = 0;
		virtual void debugOutputStats(Log& m, const char* instanceName) const = 0;
	};
}


inline void* operator new(size_t size, dimgel::alloc::MemoryManager& mm) {
	return mm.allocate(size, 0);
}

// Called automatically if constructor throws exception.
inline void operator delete(void* p, dimgel::alloc::MemoryManager& mm) {
	return mm.free(p, 0);
}
