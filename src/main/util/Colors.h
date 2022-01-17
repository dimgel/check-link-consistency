#pragma once

#include "StringRef.h"


namespace dimgel {

	struct Colors final {
		const StringRef off;

		const StringRef blue;
		const StringRef cyan;
		const StringRef green;
		const StringRef red;
		const StringRef white;
		const StringRef yellow;


		static Colors enabled;
		static Colors disabled;
	};
}
