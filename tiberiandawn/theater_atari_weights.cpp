/*
 * theater_atari_weights.cpp — load per-theater C2P weight tables (<theater>.W16)
 * after DisplayClass::Init_Theater has copied the theater .PAL into GamePalette.
 *
 * Sibling of wsa_atari_weights.cpp: both load a 4096-byte (256x16) weight matrix
 * via CCFileClass and hand it to C2P_Install_CustomWeights() so chunky-to-planar
 * dithering uses the right source palette. Theaters without a shipped .W16 (e.g.
 * JUNGLE on stock TD) silently fall back to the compiled-in TEMPERAT weight set
 * — those theaters never load in retail data so this is a defensive-only path.
 */

#ifdef ATARI_ST

#include "function.h"
#include "c2p.h"
#include <stdio.h>

extern "C" void Theater_Atari_TryInstallC2PWeights(const char *theater_root)
{
	char w16_name[16];
	uint8_t weights[256 * 16];
	size_t i;
	CCFileClass file("");
	long got;

	if (!theater_root || !theater_root[0]) {
		return;
	}

	i = 0;
	while (theater_root[i] && i + 5 < sizeof(w16_name)) {
		w16_name[i] = theater_root[i];
		++i;
	}
	if (i == 0 || i + 5 >= sizeof(w16_name)) {
		fprintf(stderr, "Theater: warning: cannot derive .W16 name from '%s'\n", theater_root);
		return;
	}
	w16_name[i++] = '.';
	w16_name[i++] = 'W';
	w16_name[i++] = '1';
	w16_name[i++] = '6';
	w16_name[i] = '\0';

	/*
	 * Reset to the compiled-in TEMPERAT defaults first so a missing/short .W16
	 * leaves C2P in a known-good state (rather than whatever the previous theater
	 * or WSA-test playback installed).
	 */
	C2P_Clear_CustomWeights();
	file.Set_Name(w16_name);
	if (!file.Is_Available()) {
		fprintf(stderr, "Theater: warning: no C2P weights found (%s); using TEMPERAT defaults\n", w16_name);
		return;
	}
	if (!file.Open(READ)) {
		fprintf(stderr, "Theater: warning: failed opening C2P weights (%s)\n", w16_name);
		return;
	}
	got = file.Read(weights, (long)sizeof(weights));
	file.Close();
	if (got != (long)sizeof(weights)) {
		fprintf(stderr, "Theater: warning: invalid C2P weights size in %s (got %ld, expected %u)\n",
		        w16_name, got, (unsigned)sizeof(weights));
		return;
	}
	if (!C2P_Install_CustomWeights(weights)) {
		fprintf(stderr, "Theater: warning: rejected C2P weights (%s)\n", w16_name);
	}
}

#endif
