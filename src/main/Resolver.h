#pragma once

#include "data.h"


namespace dimgel {

	class Resolver {
		Context& ctx;
		Data& data;

	public:
		Resolver(Context& ctx, Data& data) : ctx(ctx), data(data) {}

		// Returns true if all OK.
		bool execute();

		void dumpErrors();
	};
}
