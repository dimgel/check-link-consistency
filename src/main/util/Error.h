#pragma once

#include <stdexcept>


namespace dimgel {

	// See also: Abort, Log.
	// For message without parameters, throw std::runtime_error() itself.
	class Error : public std::runtime_error {
	public:
		// __attribute__(): format arg is 2 because arg 1 is `this`.
		Error(const char* format, ...) __attribute__((format(printf, 2, 3)));
	};
}
