#include <assert.h>
#include "../main/util/Error.h"
#include "../main/util/util.h"

using namespace dimgel;


static void f1(int lineNo, const std::string& source, const std::string& expected) {
	char buf[PATH_MAX];
	auto size = util::normalizePath(source.c_str(), buf);
	if (buf != expected) {
		throw Error("Assertion failed @ line %d: buf \"%s\" != expected \"%s\"", lineNo, source.c_str(), expected.c_str());
	}
	if (size != expected.length()) {
		throw Error("Assertion failed @ line %d: size %lu != expected %lu", lineNo, ulong{size}, ulong{expected.length()});
	}
};

static void f2(int line, const std::string& source, const std::string& expected) {
	f1(line, source, expected);
	f1(line, "/" + source, "/" + expected);

};

#define F1(source, expected) f1(__LINE__, source, expected)
#define F2(source, expected) f2(__LINE__, source, expected)


void test_util_normalizePath() {
	F2("", "");
	F2("a", "a");
	F2("a/b", "a/b");
	F2("a/b/", "a/b/");
	F2("a/b/c", "a/b/c");
	F2("a/b/c/", "a/b/c/");
	F2("a/b/cde", "a/b/cde");
	F2("a/b/cde/", "a/b/cde/");
	F2("a/bcd", "a/bcd");
	F2("a/bcd/", "a/bcd/");
	F2("a/bcd/e", "a/bcd/e");
	F2("a/bcd/e/", "a/bcd/e/");
	F2("a/bcd/efg", "a/bcd/efg");
	F2("a/bcd/efg/", "a/bcd/efg/");
	F2("abc", "abc");
	F2("abc/def/ghi", "abc/def/ghi");

	F2("./a", "a");
	F2("./a/", "a/");
	F2("././a", "a");
	F2("././a/", "a/");
	F2("a/./d", "a/d");
	F2("a/./d/", "a/d/");
	F2("a/././d", "a/d");
	F2("a/./d/.", "a/d");
	F2("a/./d/./", "a/d/");
	F2("a/./d/./.", "a/d");
	F2("a/./d/././", "a/d/");
	F2("./abc", "abc");
	F2("./abc/", "abc/");
	F2("././abc", "abc");
	F2("././abc/", "abc/");
	F2("abc/./def", "abc/def");
	F2("abc/./def/", "abc/def/");
	F2("abc/././def", "abc/def");
	F2("abc/././def/", "abc/def/");
	F2("abc/./def/.", "abc/def");
	F2("abc/./def/./", "abc/def/");
	F2("abc/./def/./.", "abc/def");
	F2("abc/./def/././", "abc/def/");

	F2("a/..", "");
	F2("a/../", "");   // Important: trailing slash must not become leading, i.e. must not transform relative path to absolute.
	F2("a/./b/.././..", "");
	F2("a/./b/.././../", "");
	F2("a/b/.././../c/d/./.././e", "c/e");
	F2("a/b/.././../c/d/./.././e/", "c/e/");
	F2("abc/..", "");
	F2("abc/../", "");
	F2("abc/./def/.././..", "");
	F2("abc/./def/.././../", "");
	F2("abc/def/.././../ghi/jkl/./.././mno", "ghi/mno");
	F2("abc/def/.././../ghi/jkl/./.././mno/", "ghi/mno/");

	F1("///abc//.///def////./////.", "/abc/def");   // F1(): merge leading slashes.
	F2("abc//.///def////./////.", "abc/def");
	F2("abc//.///def////./////.//////", "abc/def/");

	try { F2("..",  ""); assert(false); } catch (...) {}
	try { F2("../", ""); assert(false); } catch (...) {}
	try { F2("../abc",  ""); assert(false); } catch (...) {}
	try { F2("../abc/", ""); assert(false); } catch (...) {}
	try { F2("abc/../../def",  ""); assert(false); } catch (...) {}
	try { F2("abc/../../def/",  ""); assert(false); } catch (...) {}
	try { F2("a/../b/c/../../..",  ""); assert(false); } catch (...) {}
	try { F2("a/../b/c/../../../",  ""); assert(false); } catch (...) {}
}
