#pragma once

#include <stdexcept>


namespace dimgel {

	// Signals error without message.
	// See also: Error.
	class Abort : public std::runtime_error {
	public:
		Abort() : std::runtime_error("Abort") {}
	};
}
