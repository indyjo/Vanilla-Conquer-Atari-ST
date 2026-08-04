/*
 * Interactive: mix two .AUD samples through the production STE DMA path (audio_ste.cpp).
 *
 * Servicing: Audio_Init installs the VBL hook for DMA-ring mix; the main thread
 * calls Sound_Callback (page-ring refill + Sound_Maintenance) each wait tick.
 *
 * Voice 0 is started first (cold DMA arm), voice 1 overlays — same order as theme +
 * EVA speech (File_Stream / theme uses PRIORITY_MAX 255, EVA Play_Sample 254).
 */

#include "st_audio_mix_test.h"

#include "st_audio_asset_autotest.h"
#include "st_mix_minimal.h"

#include "function.h"
#include "audio.h"
#include "misc.h"

#include <mint/osbind.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	char const *mix;
	char const *aud;
	char const *label;
} StAudioMixCatalogEntry;

static StAudioMixCatalogEntry const k_catalog[] = {
	{ "SCORES.MIX", "AOI.AUD", "Act on Instinct" },
	{ "SCORES.MIX", "IND2.AUD", "Industrial2" },
	{ "SCORES.MIX", "TROUBLE.AUD", "Trouble" },
	{ "SPEECH.MIX", "NEWOPT1.AUD", "New construction (SPEECH)" },
	{ "SOUNDS.MIX", "NEWOPT1.AUD", "New construction (SOUNDS)" },
	{ "SCOUNDS.MIX", "NEWOPT1.AUD", "New construction (SCOUNDS)" },
	{ "SCOUNDS.MIX", "CONSTRU1.AUD", "Construction complete" },
	{ "SOUNDS.MIX", "CONSTRU1.AUD", "Construction complete (SOUNDS)" },
	{ "SCOUNDS.MIX", "BEEPY6.AUD", "Beepy6" },
	{ "SOUNDS.MIX", "BEEPY6.AUD", "Beepy6 (SOUNDS)" },
	{ "SCOUNDS.MIX", "TEXT2.AUD", "Text2" },
	{ "TRANSIT.MIX", "STRUGGLE.AUD", "Struggle (transit)" },
	{ "TRANSIT.MIX", "WIN1.AUD", "Win1" },
};

static int st_catalog_count(void)
{
	return (int)(sizeof(k_catalog) / sizeof(k_catalog[0]));
}

static void st_catalog_line(int idx, char *buf, size_t buflen)
{
	if (!buf || buflen == 0) {
		return;
	}
	if (idx < 0 || idx >= st_catalog_count()) {
		buf[0] = '\0';
		return;
	}
	StAudioMixCatalogEntry const *e = &k_catalog[idx];
	snprintf(buf, buflen, "%s / %s", e->mix, e->aud);
}

static BOOL st_audio_init_game_rate(void)
{
	return Audio_Init(NULL, 8, FALSE, 11025 * 2, 0);
}

enum { ST_DUAL_MIX_MAX_VBL = 45000 };

static void st_drain_console_input(void)
{
	/* Device 2 = console keyboard (BIOS). */
	while (Bconstat(2) != 0) {
		(void)Cnecin();
	}
}

static int st_wait_dual(void const *a, void const *b)
{
	/* Flush pending menu keystrokes so playback doesn't auto-stop immediately. */
	st_drain_console_input();

	for (int i = 0; i < ST_DUAL_MIX_MAX_VBL; i++) {
		Wait_Vert_Blank();
		Sound_Callback();

		if (Bconstat(2) != 0) {
			long w = Cnecin();
			int ch = (int)(w & 0xFF);
			if (ch == 27 || ch == ' ' || ch == 'q' || ch == 'Q') {
				if (a) {
					Stop_Sample_Playing(a);
				}
				if (b) {
					Stop_Sample_Playing(b);
				}
				return -1;
			}
		}

		if (!Is_Sample_Playing(a) && !Is_Sample_Playing(b)) {
			return 0;
		}
	}
	printf("FAIL dual mix timeout\n");
	if (a) {
		Stop_Sample_Playing(a);
	}
	if (b) {
		Stop_Sample_Playing(b);
	}
	return 1;
}

static int st_load_catalog_entry(int idx, unsigned char **out, size_t *out_len)
{
	if (idx < 0 || idx >= st_catalog_count() || !out || !out_len) {
		return -1;
	}
	*out = NULL;
	*out_len = 0;
	StAudioMixCatalogEntry const *e = &k_catalog[idx];
	int const mx = st_mix_extract_file(e->mix, e->aud, out, out_len);
	if (mx != 0 || !*out || *out_len < 12u) {
		if (*out) {
			free(*out);
			*out = NULL;
			*out_len = 0;
		}
		return mx != 0 ? mx : -2;
	}
	return 0;
}

static int st_pick_volume(char const *voice_name)
{
	printf("\n%s volume:\n", voice_name);
	printf("  1=64  2=128  3=192  4=255 (default)\n");
	printf("  or type 0-9 digits then Enter\n");
	printf("Choice: ");
	fflush(stdout);

	int value = -1;
	int digits = 0;
	for (;;) {
		long w = Crawcin();
		int ch = (int)(w & 0xFF);
		printf("%c\n", (ch >= 32 && ch < 127) ? ch : '?');

		if (ch == 27) {
			return -1;
		}
		if (ch == '\r' || ch == '\n') {
			if (digits > 0) {
				if (value < 0) {
					value = 0;
				}
				if (value > 255) {
					value = 255;
				}
				return value;
			}
			return 255;
		}
		if (ch >= '1' && ch <= '4' && digits == 0) {
			static int const presets[] = {64, 128, 192, 255};
			return presets[ch - '1'];
		}
		if (ch >= '0' && ch <= '9') {
			int const d = ch - '0';
			digits++;
			if (value < 0) {
				value = d;
			} else {
				value = value * 10 + d;
			}
			if (value > 255) {
				value = 255;
			}
			continue;
		}
		printf("Unknown key.\n");
	}
}

static int st_pick_catalog_index(char const *title)
{
	int const total = st_catalog_count();
	int page = 0;
	enum { PAGE = 9 };

	for (;;) {
		int start = page * PAGE;
		if (start >= total) {
			page = 0;
			start = 0;
		}
		int end = start + PAGE;
		if (end > total) {
			end = total;
		}

		printf("\n");
		printf("---- %s (%d-%d of %d) ----\n", title, start + 1, end, total);
		for (int i = start; i < end; i++) {
			char line[44];
			st_catalog_line(i, line, sizeof(line));
			printf("%d %s\n", (i - start) + 1, k_catalog[i].label);
			printf("   %s\n", line);
		}
		if (end < total) {
			printf("n next  p prev  0 cancel\n");
		} else {
			printf("0 cancel\n");
		}
		printf("Choice: ");
		fflush(stdout);

		long w = Crawcin();
		int ch = (int)(w & 0xFF);
		printf("%c\n", (ch >= 32 && ch < 127) ? ch : '?');

		if (ch == '0' || ch == 27) {
			return -1;
		}
		if ((ch == 'n' || ch == 'N') && end < total) {
			page++;
			continue;
		}
		if ((ch == 'p' || ch == 'P') && page > 0) {
			page--;
			continue;
		}
		if (ch >= '1' && ch <= '9') {
			int pick = ch - '0';
			int idx = start + pick - 1;
			if (idx >= start && idx < end) {
				return idx;
			}
		}
		printf("Unknown option.\n");
	}
}

static int st_play_dual(unsigned char *buf_a, int vol_a, unsigned char *buf_b, int vol_b)
{
	if (!st_audio_init_game_rate()) {
		printf("SKIP dual mix (no STE DMA / Audio_Init)\n");
		return 2;
	}

	/* Theme path: PRIORITY_MAX (same as File_Stream_Sample_Vol on DOS/ST). */
	if (Play_Sample(buf_a, PRIORITY_MAX, vol_a, 0) < 0) {
		printf("FAIL dual mix voice A Play_Sample\n");
		Sound_End();
		return 1;
	}

	/* EVA path: priority 254 (Speak_AI). */
	if (Play_Sample(buf_b, 254, vol_b, 0) < 0) {
		printf("FAIL dual mix voice B Play_Sample\n");
		Stop_Sample_Playing(buf_a);
		Sound_End();
		return 1;
	}

	printf("Playing... (Space/Q/ESC stop)\n");
	int const wait_rc = st_wait_dual(buf_a, buf_b);
	Sound_End();
	if (wait_rc < 0) {
		printf("Stopped by user.\n");
		return 0;
	}
	if (wait_rc > 0) {
		return 1;
	}
	printf("Done.\n");
	return 0;
}

static int st_preset_load_newopt1(unsigned char **buf_b, size_t *len_b, char const **hit_mix)
{
	static char const *const k_newopt_mixes[] = {
		"SPEECH.MIX",
		"SOUNDS.MIX",
		"SCOUNDS.MIX",
		NULL,
	};
	return st_aud_load_entry("NEWOPT1.AUD", k_newopt_mixes, buf_b, len_b, hit_mix);
}

static int st_preset_aoi_newopt(void)
{
	unsigned char *buf_a = NULL;
	unsigned char *buf_b = NULL;
	size_t len_a = 0;
	size_t len_b = 0;
	char const *newopt_mix = NULL;

	int const mx_a = st_mix_extract_file("SCORES.MIX", "AOI.AUD", &buf_a, &len_a);
	if (mx_a != 0 || !buf_a || len_a < 12u) {
		printf("SKIP preset: SCORES.MIX/AOI.AUD (%s)\n", st_mix_extract_errmsg(mx_a));
		if (buf_a) {
			free(buf_a);
		}
		return 2;
	}
	if (st_preset_load_newopt1(&buf_b, &len_b, &newopt_mix) != 0) {
		printf("SKIP preset: NEWOPT1.AUD (need SPEECH.MIX)\n");
		free(buf_a);
		return 2;
	}

	printf("\nPreset: AOI + NEWOPT1 @ 255/255 (%s)\n", newopt_mix ? newopt_mix : "SOUNDS.MIX");
	int rc = st_play_dual(buf_a, 255, buf_b, 255);
	free(buf_a);
	free(buf_b);
	return rc;
}

int st_run_interactive_audio_dual_mix(void)
{
	/*
	 * Caller (audio submenu) stays in supervisor mode. Do not call Super/SuperToUser here:
	 * nested SuperToUser drops back to user mode and the next conterm/Crawcin access faults $484.
	 */
	for (;;) {
		printf("\n");
		printf("-------- Dual-sample mix test --------\n");
		printf("1 Pick voice A then B (custom volumes)\n");
		printf("p Preset: Act on Instinct + New construction @255\n");
		printf("0 Back\n");
		printf("Choice: ");
		fflush(stdout);

		long w = Crawcin();
		int ch = (int)(w & 0xFF);
		printf("%c\n", (ch >= 32 && ch < 127) ? ch : '?');

		if (ch == '0' || ch == 27) {
			break;
		}
		if (ch == 'p' || ch == 'P') {
			(void)st_preset_aoi_newopt();
			continue;
		}
		if (ch != '1') {
			printf("Unknown option.\n");
			continue;
		}

		int const idx_a = st_pick_catalog_index("Voice A (theme / bed)");
		if (idx_a < 0) {
			continue;
		}
		int vol_a = st_pick_volume("Voice A");
		if (vol_a < 0) {
			continue;
		}

		int const idx_b = st_pick_catalog_index("Voice B (overlay / speech)");
		if (idx_b < 0) {
			continue;
		}
		int vol_b = st_pick_volume("Voice B");
		if (vol_b < 0) {
			continue;
		}

		unsigned char *buf_a = NULL;
		unsigned char *buf_b = NULL;
		size_t len_a = 0;
		size_t len_b = 0;
		int const mx_a = st_load_catalog_entry(idx_a, &buf_a, &len_a);
		if (mx_a != 0) {
			printf("SKIP voice A %s (%s)\n", k_catalog[idx_a].mix, st_mix_extract_errmsg(mx_a));
			continue;
		}
		int const mx_b = st_load_catalog_entry(idx_b, &buf_b, &len_b);
		if (mx_b != 0) {
			printf("SKIP voice B %s (%s)\n", k_catalog[idx_b].mix, st_mix_extract_errmsg(mx_b));
			free(buf_a);
			continue;
		}

		char line_a[44];
		char line_b[44];
		st_catalog_line(idx_a, line_a, sizeof(line_a));
		st_catalog_line(idx_b, line_b, sizeof(line_b));
		printf("\nMix A=%s vol=%d\n", line_a, vol_a);
		printf("    B=%s vol=%d\n", line_b, vol_b);

		(void)st_play_dual(buf_a, vol_a, buf_b, vol_b);
		free(buf_a);
		free(buf_b);
	}

	return 0;
}
