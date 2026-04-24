/*
 * Central MIX registration/cache for ST test harness.
 */

#include "function.h"
#include "st_mix_register.h"

#include <stdio.h>

int st_tests_register_mixes_once(void)
{
	static int s_initialized = 0;
	static MixFileClass *s_conquer = NULL;
	static MixFileClass *s_local = NULL;
	static MixFileClass *s_temperat = NULL;

	if (s_initialized) {
		return 0;
	}
	s_initialized = 1;

	int rc = 0;

	s_conquer = new MixFileClass("CONQUER.MIX");
	s_local = new MixFileClass("LOCAL.MIX");
	s_temperat = new MixFileClass("TEMPERAT.MIX");

	if (!s_conquer || !s_conquer->Cache()) {
		printf("StMixReg: WARN cannot cache CONQUER.MIX\n");
		rc = -1;
	}
	if (!s_local || !s_local->Cache()) {
		printf("StMixReg: WARN cannot cache LOCAL.MIX\n");
		rc = -1;
	}
	if (!s_temperat || !s_temperat->Cache()) {
		printf("StMixReg: WARN cannot cache TEMPERAT.MIX\n");
		rc = -1;
	}
	return rc;
}
