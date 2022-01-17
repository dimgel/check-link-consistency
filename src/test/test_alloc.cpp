#include <assert.h>
#include "../main/util/alloc/Arena.h"
#include "../main/util/alloc/String.h"

using namespace dimgel::alloc;


// ----------------------------------------------------------------------------------------------------------------------------------------


static void test_alloc_Arena() {
	Arena a(12);
	assert(a.debugGetNumPages() == 1);
	auto o0 = a.debugGetOffset();

	a.allocate(4, 4);
	assert(a.debugGetNumPages() == 1);
	assert(a.debugGetOffset() == o0 + 4);

	a.allocate(3, 1);
	assert(a.debugGetNumPages() == 1);
	assert(a.debugGetOffset() == o0 + 7);

	a.allocate(4, 4);
	assert(a.debugGetNumPages() == 1);
	assert(a.debugGetOffset() == o0 + 12);

	a.allocate(1, 1);
	assert(a.debugGetNumPages() == 2);
	assert(a.debugGetOffset() == o0 + 1);

	a.allocate(8, 4);
	assert(a.debugGetNumPages() == 2);
	assert(a.debugGetOffset() == o0 + 12);

	a.allocate(8, 8);
	assert(a.debugGetNumPages() == 3);
	assert(a.debugGetOffset() == o0 + 8);

	a.allocate(8, 8);
	assert(a.debugGetNumPages() == 4);
	assert(a.debugGetOffset() == o0 + 8);
}


// ----------------------------------------------------------------------------------------------------------------------------------------


static void test_alloc_String() {
	Arena a(64);
	assert(a.debugGetNumPages() == 1);
	auto o0 = a.debugGetOffset();

	String s1 {a, "Hello world!"};
	assert(s1 == "Hello world!");
	assert(a.debugGetNumPages() == 1);
	assert(a.debugGetOffset() == o0 + 12 + 1);
	auto o1 = a.debugGetOffset();

	String s2 {a, {s1.sv(), " Goodbye =)", " Ku."}};
	assert(s2 == "Hello world! Goodbye =) Ku.");
	assert(a.debugGetNumPages() == 1);
	assert(a.debugGetOffset() == o1 + 12 + 11 + 4 + 1);
}

// ----------------------------------------------------------------------------------------------------------------------------------------


void test_alloc() {
	test_alloc_Arena();
	test_alloc_String();
}
