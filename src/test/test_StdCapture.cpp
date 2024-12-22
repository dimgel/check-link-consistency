#undef NDEBUG

#include <assert.h>
#include <iostream>
#include "../main/util/StdCapture.h"


void test_StdCapture() {

	auto out = dimgel::StdCapture::createStdOut();
	// without '\n' and flushing buffer, StdCapture must catch it anyway:
	std::cout << "Hello world!";
	assert(out.get() == "Hello world!");

	auto err = dimgel::StdCapture::createStdErr();
	std::cerr << "Error found.";
	std::cout << "Everything ok.\n";
	assert(out.get() == "Everything ok.\n");
	assert(err.get() == "Error found.");
}
