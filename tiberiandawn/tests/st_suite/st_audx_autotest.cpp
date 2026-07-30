/*
 * AUDX on-target smoke: small SFX via page cache; one score via file stream.
 * Skips when remacked packs / pool*.bin are absent.
 */

#include "st_audx_autotest.h"

#include "function.h"
#include "audio.h"
#include "audx/audx.h"
#include "audx/audx_page_cache.h"
#include "st_mix_minimal.h"
#include "st_mix_register.h"
#include "st_audio_asset_autotest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	ST_AUDX_PASS = 0,
	ST_AUDX_FAIL = 1,
	ST_AUDX_SKIP = 2
};

enum { ST_AUDX_SCORE_SMOKE_VBL = 150 };

static BOOL st_audx_audio_init(void)
{
	return Audio_Init(NULL, 8, FALSE, 11025 * 2, 0);
}

static int st_audx_pool_available(uint16_t pool_id)
{
	char name[16];
	if (!AUDX_Format_Pool_Name(pool_id, name, sizeof(name))) {
		return 0;
	}
	return CCFileClass(name).Is_Available() ? 1 : 0;
}

static void st_audx_wait_sample(void const *sample, int max_vbl)
{
	for (int i = 0; i < max_vbl && Is_Sample_Playing(sample); i++) {
		Wait_Vert_Blank();
		Sound_Maintenance();
	}
}

/*
 * Small SFX: AUDX meta from SOUNDS.MIX, payload via page cache (pool0005.bin).
 */
static int st_audx_test_sfx_cache(void)
{
	static char const *const tries[] = { "CLOCK1.AUD", "BEEPY6.AUD", "TEXT2.AUD", NULL };
	unsigned char *raw = NULL;
	size_t raw_len = 0;
	char const *hit = NULL;
	int ti;

	if (!CCFileClass("SOUNDS.MIX").Is_Available() || !st_audx_pool_available(AUDX_POOL_ID_SOUNDS)) {
		printf("SKIP AUDX SFX (SOUNDS.MIX / pool0005.bin missing)\n");
		return ST_AUDX_SKIP;
	}

	for (ti = 0; tries[ti]; ti++) {
		if (raw) {
			free(raw);
			raw = NULL;
			raw_len = 0;
		}
		if (st_mix_extract_file("SOUNDS.MIX", tries[ti], &raw, &raw_len) != 0 || !raw) {
			continue;
		}
		if (raw_len >= AUDX_PREFIX_SIZE && AUDX_Is_Meta(raw)) {
			hit = tries[ti];
			break;
		}
	}
	if (!hit || !raw) {
		if (raw) {
			free(raw);
		}
		printf("SKIP AUDX SFX (no AUDX meta in SOUNDS.MIX)\n");
		return ST_AUDX_SKIP;
	}

	{
		AudxPrefix const *pfx = AUDX_As_Prefix(raw);
		if (pfx->size > AUDX_PAGE_CACHE_MAX) {
			printf("SKIP AUDX SFX %s (payload > cache threshold)\n", hit);
			free(raw);
			return ST_AUDX_SKIP;
		}
	}

	if (AUDX_Page_Cache_Init() != 0) {
		printf("FAIL AUDX page cache init\n");
		free(raw);
		return ST_AUDX_FAIL;
	}
	if (!st_audx_audio_init()) {
		printf("SKIP AUDX SFX (no STE DMA / Audio_Init)\n");
		AUDX_Page_Cache_Shutdown();
		free(raw);
		return ST_AUDX_SKIP;
	}

	if (Play_Sample(raw, 255, 0xFF, 0) < 0) {
		printf("FAIL AUDX Play_Sample cache %s\n", hit);
		Sound_End();
		AUDX_Page_Cache_Shutdown();
		free(raw);
		return ST_AUDX_FAIL;
	}
	st_audx_wait_sample(raw, 45000);
	if (Is_Sample_Playing(raw)) {
		printf("FAIL AUDX SFX timeout %s\n", hit);
		Stop_Sample_Playing(raw);
		Sound_End();
		AUDX_Page_Cache_Shutdown();
		free(raw);
		return ST_AUDX_FAIL;
	}

	Sound_End();
	AUDX_Page_Cache_Shutdown();
	free(raw);
	printf("PASS AUDX SFX page-cache %s\n", hit);
	return ST_AUDX_PASS;
}

/*
 * Score: Cache SCORES.MIX meta, stream large AUDX via File_Stream (pool0007.bin).
 * Smoke: start playback, service VBLs briefly, then stop.
 */
static int st_audx_test_score_file(void)
{
	static char const *const tries[] = { "IND2.AUD", "AOI.AUD", "VALKYRIE.AUD", NULL };
	void const *meta = NULL;
	char const *hit = NULL;
	int ti;

	if (!CCFileClass("SCORES.MIX").Is_Available() || !st_audx_pool_available(AUDX_POOL_ID_SCORES)) {
		printf("SKIP AUDX score (SCORES.MIX / pool0007.bin missing)\n");
		return ST_AUDX_SKIP;
	}

	(void)st_tests_register_mixes_once();
	if (!MFCD::Cache("SCORES.MIX")) {
		printf("SKIP AUDX score (cannot Cache SCORES.MIX)\n");
		return ST_AUDX_SKIP;
	}

	for (ti = 0; tries[ti]; ti++) {
		meta = MFCD::Retrieve(tries[ti]);
		if (meta && AUDX_Is_Meta(meta)) {
			AudxPrefix const *pfx = AUDX_As_Prefix(meta);
			if (pfx->size > AUDX_PAGE_CACHE_MAX) {
				hit = tries[ti];
				break;
			}
			if (!hit) {
				hit = tries[ti];
			}
		}
		meta = NULL;
	}
	if (!hit) {
		printf("SKIP AUDX score (no AUDX meta in SCORES.MIX)\n");
		return ST_AUDX_SKIP;
	}
	meta = MFCD::Retrieve(hit);
	if (!meta || !AUDX_Is_Meta(meta)) {
		printf("SKIP AUDX score retrieve %s\n", hit);
		return ST_AUDX_SKIP;
	}

	{
		AudxPrefix const *pfx = AUDX_As_Prefix(meta);
		if (pfx->size <= AUDX_PAGE_CACHE_MAX) {
			printf("SKIP AUDX score %s (size <= 64KiB; want file path)\n", hit);
			return ST_AUDX_SKIP;
		}
	}

	(void)AUDX_Page_Cache_Init();

	if (!st_audx_audio_init()) {
		printf("SKIP AUDX score (no STE DMA / Audio_Init)\n");
		AUDX_Page_Cache_Shutdown();
		return ST_AUDX_SKIP;
	}

	if (File_Stream_Sample_Vol(hit, 0xFF, FALSE) < 0) {
		printf("FAIL AUDX File_Stream score %s\n", hit);
		Sound_End();
		AUDX_Page_Cache_Shutdown();
		return ST_AUDX_FAIL;
	}

	st_audx_wait_sample(meta, ST_AUDX_SCORE_SMOKE_VBL);
	if (Is_Sample_Playing(meta)) {
		Stop_Sample_Playing(meta);
	}

	Sound_End();
	AUDX_Page_Cache_Shutdown();
	printf("PASS AUDX score file-stream %s\n", hit);
	return ST_AUDX_PASS;
}

int st_run_audx_autotest(void)
{
	st_conterm_keyclick_mute_push();

	int const sfx = st_audx_test_sfx_cache();
	if (sfx == ST_AUDX_FAIL) {
		st_conterm_keyclick_mute_pop();
		return ST_AUDX_FAIL;
	}

	int const score = st_audx_test_score_file();
	st_conterm_keyclick_mute_pop();
	if (score == ST_AUDX_FAIL) {
		return ST_AUDX_FAIL;
	}

	if (sfx == ST_AUDX_SKIP && score == ST_AUDX_SKIP) {
		printf("SKIP AUDX (no remacked audio packs)\n");
		return ST_AUDX_SKIP;
	}
	/* One path skipped is OK if the other passed. */
	return ST_AUDX_PASS;
}
