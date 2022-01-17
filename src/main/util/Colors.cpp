#include "Colors.h"


namespace dimgel {

	constinit Colors Colors::enabled {
		.off = "\x1b[0m",
		.blue = "\x1b[34;1m",
		.cyan = "\x1b[36m",
		.green = "\x1b[32m",
		.red = "\x1b[31;1m",
		.white = "\x1b[0;1m",
		.yellow = "\x1b[33m"
	};

	constinit Colors Colors::disabled {
		.off = "",
		.blue = "",
		.cyan = "",
		.green = "",
		.red = "",
		.white = "",
		.yellow = ""
	};
}
