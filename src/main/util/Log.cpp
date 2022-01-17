#include <string.h>
#include "Log.h"


namespace dimgel {

	// Patially copy-pasted into Error::Error().
	void Log::impl(int fd, const char* pfx, StringRef color, const char* format, va_list args) {

		// Must be large enough for long EXEC command lines.
		char buf[16384];
		char* s = buf;

		memcpy(s, color.cp(), color.length());  s += color.length();
		memcpy(s, pfx, 4);  s += 4;
		memcpy(s, colors.off.cp(), colors.off.length());  s += colors.off.length();
		memcpy(s, "  ", 2);  s += 2;

		// -3 chars for "...", -1 char for '\n', +1 char for vsnprintf()'s terminating '\0', 0 chars for my own '\0' because I use write() instead of puts().
		int n = sizeof(buf) - (s - buf) - 3;

		// `man vsnprintf`: writes at most n bytes, including '\0'; returns how many WOULD be written without size limit, NOT including '\0'.
		// Experimented: it prints terminating '\0' into last available buf byte if overflown.
		// It means: actual buffer size is (n - 1), and if x == n then buffer is already overflown.
		int x = vsnprintf(s, n, format, args);

		// (x < 0) is I/O error, not possible when printing to string.
		s += std::min(x, n - 1);
		if (x >= n) {
			memcpy(s, "...", 3);  s += 3;
		}
		*s++ = '\n';

		// Atomic function, no need for extra synchronization.
		write(fd, buf, s - buf);
	}
}
