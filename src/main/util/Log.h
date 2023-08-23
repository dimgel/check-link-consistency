#pragma once

#include <unistd.h>
#include <stdarg.h>
#include "Colors.h"


namespace dimgel {

	// See also: Error.
	class Log final {
		const Colors& colors;

		// ATTENTION! All these must be 4 chars length, it's hardcoded in impl().
		static constexpr const char* pfxDEBG = "DEBG";
		static constexpr const char* pfxINFO = "INFO";
		static constexpr const char* pfxEXEC = "EXEC";
		static constexpr const char* pfxWARN = "WARN";
		static constexpr const char* pfxERR  = "ERR ";

		void impl(int fd, const char*, StringRef color, const char* format, va_list args);

	public:
		Log(const Colors& colors) : colors(colors) {}

		// __attribute__(): format arg is 2 because arg 1 is `this`.
		void debug(const char* format, ...) noexcept __attribute__((format(printf, 2, 3))) { va_list args;  va_start(args, format);  impl(STDOUT_FILENO, pfxDEBG, colors.blue,   format, args);  va_end(args); }
		void info (const char* format, ...) noexcept __attribute__((format(printf, 2, 3))) { va_list args;  va_start(args, format);  impl(STDOUT_FILENO, pfxINFO, colors.green,  format, args);  va_end(args); }
		void exec (const char* format, ...) noexcept __attribute__((format(printf, 2, 3))) { va_list args;  va_start(args, format);  impl(STDERR_FILENO, pfxEXEC, colors.cyan,   format, args);  va_end(args); }
		void warn (const char* format, ...) noexcept __attribute__((format(printf, 2, 3))) { va_list args;  va_start(args, format);  impl(STDERR_FILENO, pfxWARN, colors.yellow, format, args);  va_end(args); }
		void error(const char* format, ...) noexcept __attribute__((format(printf, 2, 3))) { va_list args;  va_start(args, format);  impl(STDERR_FILENO, pfxERR , colors.red,    format, args);  va_end(args); }

		// GCC sometimes warns about arguments even without __attribute__() but sometimes does not; CLANG does not warn.
//		using F = void(Log::*)(const char* format, ...) noexcept;
		// GCC parse error:
//		using F = void(Log::* __attribute__((format(printf, 2, 3))))(const char* format, ...) noexcept;
		// GCC warns about arguments; CLANG warns about invalid attribute here and does not warn about arguments:
		typedef void (Log::*F)(const char* format, ...) noexcept __attribute__((format(printf, 2, 3)));
	};
}
