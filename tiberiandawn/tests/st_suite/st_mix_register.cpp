/*
 * Central MIX registration/cache for ST test harness.
 */

#include "function.h"
#include "st_mix_register.h"

#include <new>
#include <stdio.h>

int st_tests_register_mixes_once(void)
{
	static int s_initialized = 0;
	static MFCD *s_conquer = NULL;
	static MFCD *s_local = NULL;
	static MFCD *s_temperat = NULL;
	static MFCD *s_general = NULL;
	static MFCD *s_speech = NULL;
	static MFCD *s_sounds = NULL;
	static MFCD *s_scores = NULL;

	if (s_initialized) {
		return 0;
	}
	s_initialized = 1;

	int rc = 0;

	s_conquer = new (std::nothrow) MFCD("CONQUER.MIX");
	s_local = new (std::nothrow) MFCD("LOCAL.MIX");
	s_temperat = new (std::nothrow) MFCD("TEMPERAT.MIX");
	s_general = new (std::nothrow) MFCD("GENERAL.MIX");

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
	/* Keep GENERAL.MIX registered but avoid caching its large payload in st-tests. */
	if (!s_general) {
		printf("StMixReg: WARN cannot register GENERAL.MIX\n");
		rc = -1;
	}

	/* EVA / scores / SFX archives (disk extract + optional MFCD::Retrieve). */
	if (CCFileClass("SPEECH.MIX").Is_Available()) {
		s_speech = new (std::nothrow) MFCD("SPEECH.MIX");
	}
	if (CCFileClass("SOUNDS.MIX").Is_Available()) {
		s_sounds = new (std::nothrow) MFCD("SOUNDS.MIX");
	}
	if (CCFileClass("SCORES.MIX").Is_Available()) {
		s_scores = new (std::nothrow) MFCD("SCORES.MIX");
	}

	return rc;
}
