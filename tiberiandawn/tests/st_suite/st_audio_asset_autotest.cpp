/*
 * Automated: load a real .AUD from game MIX archives, decode/play via audio_ste Play_Sample.
 * Servicing matches in-game ATARI_ST: VBL hook installed by Audio_Init; wait on Wait_Vert_Blank
 * and Sound_Maintenance (deferred teardown only, no main-thread ring refill).
 *
 * Key-click muting uses TOS conterm ($484) bit 0 via Supexec (MiNT user-mode $484 bus-errors).
 */

#include "st_audio_asset_autotest.h"

#include "function.h"
#include "audio.h"
#include "misc.h"
#include "st_mix_minimal.h"
#include "ste_aud_constants.h"
#include "audx/audx.h"
#include "audx/audx_page_cache.h"

#include <mint/osbind.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	char const *mix;
	char const *aud;
	int volume; /* <0 = 0xFF; else Play_Sample volume (Choose_Side static = 64) */
} StAudioAssetTry;

enum {
	ST_AUDIO_RESULT_PASS = 0,
	ST_AUDIO_RESULT_FAIL = 1,
	ST_AUDIO_RESULT_SKIP = 2
};

static StAudioAssetTry const k_audio_tries[] = {
	{ "SCOUNDS.MIX", "CLOCK1.AUD", -1 },
	{ "SCOUNDS.MIX", "BEEPY6.AUD", -1 },
	{ "SCOUNDS.MIX", "TEXT2.AUD", -1 },
	{ "SOUNDS.MIX", "CLOCK1.AUD", -1 },
	{ "SOUNDS.MIX", "BEEPY6.AUD", -1 },
	{ "SOUNDS.MIX", "TEXT2.AUD", -1 },
	{ "SCORES.MIX", "IND2.AUD", -1 },
	{ "TRANSIT.MIX", "STRUGGLE.AUD", 64 }, /* Choose_Side intro static hiss */
	{ "TRANSIT.MIX", "WIN1.AUD", -1 },     /* THEME_WIN1 / Great Shot! */
};

int st_asset_audio_try_count(void)
{
	return (int)(sizeof(k_audio_tries) / sizeof(k_audio_tries[0]));
}

void st_asset_audio_try_label(int idx, char *buf, size_t buflen)
{
	if (!buf || buflen == 0) {
		return;
	}
	if (idx < 0 || idx >= st_asset_audio_try_count()) {
		buf[0] = '\0';
		return;
	}
	snprintf(buf, buflen, "%s / %s", k_audio_tries[idx].mix, k_audio_tries[idx].aud);
}

/* TOS conterm ($484): bit 0 = keyboard click; nested push/pop for submenu + per-run RAII */
enum { ST_CONTERM_STACK = 8 };
static unsigned char s_conterm_saved[ST_CONTERM_STACK];
static int s_conterm_depth;

static long st_conterm_push_super(void)
{
	volatile unsigned char *ct = (volatile unsigned char *)0x484UL;
	if (s_conterm_depth < ST_CONTERM_STACK) {
		s_conterm_saved[s_conterm_depth++] = *ct;
		*ct = (unsigned char)(*ct & (unsigned char)~1u);
	}
	return 0;
}

static long st_conterm_pop_super(void)
{
	volatile unsigned char *ct = (volatile unsigned char *)0x484UL;
	if (s_conterm_depth > 0) {
		*ct = s_conterm_saved[--s_conterm_depth];
	}
	return 0;
}

void st_conterm_keyclick_mute_push(void)
{
	(void)Supexec(st_conterm_push_super);
}

void st_conterm_keyclick_mute_pop(void)
{
	(void)Supexec(st_conterm_pop_super);
}

struct StKeyclickMuteRAII {
	StKeyclickMuteRAII() { st_conterm_keyclick_mute_push(); }
	~StKeyclickMuteRAII() { st_conterm_keyclick_mute_pop(); }
};

static unsigned long st_aud_payload_bytes(unsigned char const *b)
{
	unsigned long const szf = (unsigned long)b[2] | ((unsigned long)b[3] << 8) | ((unsigned long)b[4] << 16)
	    | ((unsigned long)b[5] << 24);
	unsigned long const uncomp = (unsigned long)b[6] | ((unsigned long)b[7] << 8) | ((unsigned long)b[8] << 16)
	    | ((unsigned long)b[9] << 24);
	unsigned char const compression = b[11];
	unsigned long aud_bytes = (unsigned long)STE_AUD_HDR_LEN + szf;
	if (compression == (unsigned char)STE_AUD_COMP_PCM && szf == 0UL && uncomp > 0UL) {
		aud_bytes = (unsigned long)STE_AUD_HDR_LEN + uncomp;
	}
	return aud_bytes;
}

static int st_aud_copy_malloc(void const *sample, unsigned char **out, size_t *out_len)
{
	if (!sample || !out || !out_len) {
		return -1;
	}
	unsigned char const *b = (unsigned char const *)sample;
	if (AUDX_Is_Meta(b)) {
		unsigned char *buf = (unsigned char *)malloc(AUDX_PREFIX_SIZE);
		if (!buf) {
			return -9;
		}
		memcpy(buf, b, (size_t)AUDX_PREFIX_SIZE);
		*out = buf;
		*out_len = (size_t)AUDX_PREFIX_SIZE;
		return 0;
	}
	unsigned long const aud_bytes = st_aud_payload_bytes(b);
	if (aud_bytes < (unsigned long)STE_AUD_HDR_LEN) {
		return -1;
	}
	unsigned char *buf = (unsigned char *)malloc(aud_bytes);
	if (!buf) {
		return -9;
	}
	memcpy(buf, b, (size_t)aud_bytes);
	*out = buf;
	*out_len = (size_t)aud_bytes;
	return 0;
}

int st_aud_load_entry(char const *aud_name, char const *const *mix_paths,
		unsigned char **out, size_t *out_len, char const **hit_source)
{
	if (!aud_name || !out || !out_len) {
		return -1;
	}
	*out = NULL;
	*out_len = 0;

	if (mix_paths) {
		for (int i = 0; mix_paths[i]; i++) {
			int const mx = st_mix_extract_file(mix_paths[i], aud_name, out, out_len);
			if (mx == 0 && *out && *out_len >= 12u) {
				if (hit_source) {
					*hit_source = mix_paths[i];
				}
				return 0;
			}
			if (*out) {
				free(*out);
				*out = NULL;
				*out_len = 0;
			}
		}
	}

	void const *ptr = MFCD::Retrieve(aud_name);
	if (!ptr && CCFileClass("SPEECH.MIX").Is_Available()) {
		(void)MFCD::Cache("SPEECH.MIX");
		ptr = MFCD::Retrieve(aud_name);
	}
	if (!ptr && CCFileClass("SOUNDS.MIX").Is_Available()) {
		(void)MFCD::Cache("SOUNDS.MIX");
		ptr = MFCD::Retrieve(aud_name);
	}
	if (ptr && st_aud_copy_malloc(ptr, out, out_len) == 0) {
		if (hit_source) {
			*hit_source = "MFCD";
		}
		return 0;
	}
	if (*out) {
		free(*out);
		*out = NULL;
		*out_len = 0;
	}
	return -1;
}

static BOOL st_audio_init_game_rate(void)
{
	return Audio_Init(NULL, 8, FALSE, 11025 * 2, 0);
}

/* Wall-clock cap for long score tracks (WIN1, etc.); ~15 min at 50 Hz VBL. */
enum { ST_AUDIO_MAX_VBL_WAIT = 45000 };

static void st_audio_wait_vbl_until_done(void const* sample)
{
	for (int i = 0; i < ST_AUDIO_MAX_VBL_WAIT && Is_Sample_Playing(sample); i++) {
		Wait_Vert_Blank();
		Sound_Maintenance();
	}
}

/* Returns ST_AUDIO_RESULT_*; always frees raw. */
static int st_audio_play_loaded(unsigned char *raw, char const *hit_mix, char const *hit_aud, int volume, int verbose)
{
	if (!st_audio_init_game_rate()) {
		if (verbose) {
			printf("SKIP audio (no STE DMA / Audio_Init)\n");
		}
		free(raw);
		return ST_AUDIO_RESULT_SKIP;
	}

	if (AUDX_Is_Meta(raw)) {
		AudxPrefix const *pfx = AUDX_As_Prefix(raw);
		char pool_name[16];
		if (!AUDX_Format_Pool_Name(pfx->pool_id, pool_name, sizeof(pool_name))
		    || !CCFileClass(pool_name).Is_Available()) {
			printf("SKIP audio AUDX pool missing %s:%s\n", hit_mix, hit_aud);
			Sound_End();
			free(raw);
			return ST_AUDIO_RESULT_SKIP;
		}
		if (AUDX_Page_Cache_Init() != 0) {
			printf("FAIL audio AUDX page cache init\n");
			Sound_End();
			free(raw);
			return ST_AUDIO_RESULT_FAIL;
		}
	}

	int const play_vol = (volume < 0) ? 0xFF : volume;
	if (Play_Sample(raw, 255, play_vol, 0) < 0) {
		printf("FAIL audio Play_Sample %s:%s\n", hit_mix, hit_aud);
		Sound_End();
		AUDX_Page_Cache_Shutdown();
		free(raw);
		return ST_AUDIO_RESULT_FAIL;
	}

	st_audio_wait_vbl_until_done(raw);
	if (Is_Sample_Playing(raw)) {
		printf("FAIL audio playback timeout %s:%s\n", hit_mix, hit_aud);
		Stop_Sample_Playing(raw);
		Sound_End();
		AUDX_Page_Cache_Shutdown();
		free(raw);
		return ST_AUDIO_RESULT_FAIL;
	}

	Sound_End();
	AUDX_Page_Cache_Shutdown();
	free(raw);
	if (verbose) {
		printf("PASS audio %s from %s\n", hit_aud, hit_mix);
	}
	return ST_AUDIO_RESULT_PASS;
}

int st_run_asset_audio_try_index(int idx)
{
	if (idx < 0 || idx >= st_asset_audio_try_count()) {
		return -1;
	}
	StKeyclickMuteRAII mute;
	(void)mute;

	unsigned char *raw = NULL;
	size_t raw_len = 0;
	int const mx = st_mix_extract_file(k_audio_tries[idx].mix, k_audio_tries[idx].aud, &raw, &raw_len);
	if (mx != 0 || !raw) {
		printf("SKIP audio %s:%s err=%d\n", k_audio_tries[idx].mix, k_audio_tries[idx].aud, mx);
		if (raw) {
			free(raw);
		}
		return ST_AUDIO_RESULT_SKIP;
	}
	if (raw_len < 12u) {
		printf("SKIP audio bad AUD size %s:%s\n", k_audio_tries[idx].mix, k_audio_tries[idx].aud);
		free(raw);
		return ST_AUDIO_RESULT_SKIP;
	}

	return st_audio_play_loaded(raw, k_audio_tries[idx].mix, k_audio_tries[idx].aud,
			k_audio_tries[idx].volume, 1);
}

int st_run_asset_audio_autotest_ex(int verbose)
{
	StKeyclickMuteRAII mute;
	(void)mute;

	unsigned char *raw = NULL;
	size_t raw_len = 0;
	char const *hit_mix = NULL;
	char const *hit_aud = NULL;
	int hit_volume = -1;

	for (size_t ti = 0; ti < sizeof(k_audio_tries) / sizeof(k_audio_tries[0]); ti++) {
		int const mx = st_mix_extract_file(k_audio_tries[ti].mix, k_audio_tries[ti].aud, &raw, &raw_len);
		if (mx == 0 && raw && raw_len >= 12u) {
			hit_mix = k_audio_tries[ti].mix;
			hit_aud = k_audio_tries[ti].aud;
			hit_volume = k_audio_tries[ti].volume;
			break;
		}
		if (raw) {
			free(raw);
			raw = NULL;
			raw_len = 0;
		}
	}

	if (!raw || !hit_mix || !hit_aud) {
		if (verbose) {
			printf("SKIP audio (no .AUD in tried MIXes - need e.g. SCOUNDS.MIX/SOUNDS.MIX or SCORES.MIX)\n");
		}
		return ST_AUDIO_RESULT_SKIP;
	}

	return st_audio_play_loaded(raw, hit_mix, hit_aud, hit_volume, verbose);
}

int st_run_asset_audio_autotest(void)
{
	return st_run_asset_audio_autotest_ex(1);
}
