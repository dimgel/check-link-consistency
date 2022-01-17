#pragma once

#include <stdexcept>


namespace dimgel {

	// See also: Abort, Log.
	// For message without parameters, throw std::runtime_error() itself.
	class Error : public std::runtime_error {
	public:
		Error(const char* format, ...);
	};
}
