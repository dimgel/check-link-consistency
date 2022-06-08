#pragma once

#include <functional>


namespace dimgel {

	class Finally {
		std::function<void(void)> f;
	public:
		Finally(std::function<void(void)> f) : f(std::move(f)) {}
		~Finally() { f(); }
	};
}
