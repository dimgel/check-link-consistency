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

		void debug(const char* format, ...) noexcept { va_list args;  va_start(args, format);  impl(STDOUT_FILENO, pfxDEBG, colors.blue,   format, args); }
		void info (const char* format, ...) noexcept { va_list args;  va_start(args, format);  impl(STDOUT_FILENO, pfxINFO, colors.green,  format, args); }
		void exec (const char* format, ...) noexcept { va_list args;  va_start(args, format);  impl(STDERR_FILENO, pfxEXEC, colors.cyan,   format, args); }
		void warn (const char* format, ...) noexcept { va_list args;  va_start(args, format);  impl(STDERR_FILENO, pfxWARN, colors.yellow, format, args); }
		void error(const char* format, ...) noexcept { va_list args;  va_start(args, format);  impl(STDERR_FILENO, pfxERR , colors.red,    format, args); }

		using F = void(Log::*)(const char* format, ...);
	};
}
