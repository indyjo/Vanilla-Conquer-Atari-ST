/*
 * Host test runner — single entry point for all tests under tests/host/.
 *
 * Each test_*.cpp exports int test_<name>(void) (0 = pass, non-zero = fail).
 * Add a new case by appending to the table below and linking the .cpp.
 */

#include <cstdio>

extern int test_snap_lepton_to_pixel_grid(void);

struct HostTest {
	char const* name;
	int (*fn)(void);
};

static HostTest const k_tests[] = {
	{"snap_lepton_to_pixel_grid", test_snap_lepton_to_pixel_grid},
};

int main()
{
	int failed = 0;
	unsigned const n = (unsigned)(sizeof(k_tests) / sizeof(k_tests[0]));

	std::printf("Host tests (%u):\n", n);
	for (unsigned i = 0; i < n; ++i) {
		std::printf("== %s ==\n", k_tests[i].name);
		int const rc = k_tests[i].fn();
		if (rc != 0) {
			std::printf("FAILED: %s (rc=%d)\n", k_tests[i].name, rc);
			failed = 1;
		}
	}

	if (failed) {
		std::printf("Host tests: FAIL\n");
		return 1;
	}
	std::printf("Host tests: PASS\n");
	return 0;
}
