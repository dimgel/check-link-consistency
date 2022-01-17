void test_util_normalizePath();
void test_alloc();
void test_StdCapture();
void test_util_forkExecStdCapture();


// Grouped calls are ordered by dependency order.
int main() {
	test_util_normalizePath();

	test_alloc();

	test_StdCapture();
	test_util_forkExecStdCapture();

	return 0;
}
