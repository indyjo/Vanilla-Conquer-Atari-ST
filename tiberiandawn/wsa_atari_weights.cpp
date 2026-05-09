/*
 * wsa_atari_weights.cpp — load per-WSA C2P weight tables (.W16) after Open_Animation.
 * Kept separate from wsa.cpp so CCFileClass can be used (MIX resident, MIX on-disk, loose file).
 */

#ifdef ATARI_ST

#include "function.h"
#include "c2p.h"
#include <stdio.h>
#include <string.h>

extern "C" void WSA_Atari_TryInstallC2PWeights(const char *wsa_filename)
{
	char w16_name[32];
	unsigned char weights[256 * 16];
	size_t i;
	CCFileClass file("");
	long got;

	if (!wsa_filename || !wsa_filename[0]) {
		return;
	}

	i = 0;
	while (wsa_filename[i] && wsa_filename[i] != '.' && i + 5 < sizeof(w16_name)) {
		w16_name[i] = wsa_filename[i];
		++i;
	}
	if (i + 5 >= sizeof(w16_name) || i == 0) {
		fprintf(stderr, "WSA: warning: cannot derive .W16 name from '%s'\n", wsa_filename);
		return;
	}
	w16_name[i++] = '.';
	w16_name[i++] = 'W';
	w16_name[i++] = '1';
	w16_name[i++] = '6';
	w16_name[i] = '\0';

	C2P_Clear_CustomWeights();
	file.Set_Name(w16_name);
	if (!file.Is_Available()) {
		fprintf(stderr, "WSA: warning: no custom C2P weights found (%s)\n", w16_name);
		return;
	}
	if (!file.Open(READ)) {
		fprintf(stderr, "WSA: warning: failed opening custom C2P weights (%s)\n", w16_name);
		return;
	}
	got = file.Read(weights, (long)sizeof(weights));
	file.Close();
	if (got != (long)sizeof(weights)) {
		fprintf(stderr, "WSA: warning: invalid custom C2P weights size in %s (got %ld, expected %u)\n",
		        w16_name, got, (unsigned)sizeof(weights));
		return;
	}
	if (!C2P_Install_CustomWeights(weights)) {
		fprintf(stderr, "WSA: warning: rejected custom C2P weights (%s)\n", w16_name);
	}
}

#endif
