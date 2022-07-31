#pragma once

#include <functional>


namespace dimgel {

	class Finally {
		std::function<void(void)> f;
	public:
		Finally(std::function<void(void)> f_) : f(std::move(f_)) {}
		~Finally() { f(); }
	};
}
