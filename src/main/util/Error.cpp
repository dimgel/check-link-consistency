#include <stdarg.h>
#include <string.h>
#include "Error.h"


namespace dimgel {

	// Disable warning "ISO C++ forbids braced-groups within expressions"
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wpedantic"

		// Partially copy-pasted from Log::impl().
		Error::Error(const char* format, ...) : std::runtime_error(({
			va_list args;
			va_start(args, format);

			char buf[16384];

			// Differs from Log::impl(): `-2` instead of `-3` because I don't need terminating '\n'.
			int n = sizeof(buf) - 2;

			int x = vsnprintf(buf, n, format, args);
			char* s = buf + std::min(x, n - 1);
			if (x >= n) {
				memcpy(s, "...", 3);  s += 3;
			}

			// Differs from Log::impl(): don't need '\n'.
//			*s++ = '\n';

			std::string{buf, size_t(s - buf)};
		})) {
		}
	#pragma GCC diagnostic pop
}
