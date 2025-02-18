#undef NDEBUG

#include <assert.h>
#include "../main/util/util.h"


void test_util_forkExecStdCapture() {
	{
		const char* argv[] = {"/usr/bin/echo", "abcx", nullptr};
		auto r = dimgel::util::forkExecStdCapture(argv, {.requireStatus0 = false, .captureStdOut = true, .captureStdErr = true});
		assert(r.status == 0);
		assert(r.stdOut == "abcx\n");
		assert(r.stdErr == "");
	}
	{
		const char* argv[] = {"/usr/bin/echo", "abcx", nullptr};
		auto r = dimgel::util::forkExecStdCapture(argv, {.requireStatus0 = false, .captureStdOut = true, .captureStdErr = false});
		assert(r.status == 0);
		assert(r.stdOut == "abcx\n");
		assert(r.stdErr == "");
	}
	{
		const char* argv[] = {"/usr/bin/cat", "/../bcdy", nullptr};
		auto r = dimgel::util::forkExecStdCapture(argv, {.requireStatus0 = false, .captureStdOut = true, .captureStdErr = true});
		assert(r.status == 1);
		assert(r.stdOut == "");
		assert(r.stdErr == "cat: /../bcdy: No such file or directory\n");
	}
	{
		const char* argv[] = {"/usr/bin/cat", "/../bcdy", nullptr};
		auto r = dimgel::util::forkExecStdCapture(argv, {.requireStatus0 = false, .captureStdOut = false, .captureStdErr = true});
		assert(r.status == 1);
		assert(r.stdOut == "");
		assert(r.stdErr == "cat: /../bcdy: No such file or directory\n");
	}
}
