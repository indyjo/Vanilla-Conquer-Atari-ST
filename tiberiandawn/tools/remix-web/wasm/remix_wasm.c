/*
 * remix-web WASM glue — MEMFS front end for remix_mix_file_ex().
 */

#include "remix.h"
#include "remix_shpx.h"
#include "remix_st16.h"

#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WASM_IN_PATH "/in.mix"
#define WASM_IN_B_PATH "/in_b.mix"
#define WASM_OUT_PATH "/out.mix"
#define WASM_MERGE_TMP_PATH "/out.mix.merge.tmp"
/** Input already written to MEMFS by JS (see remix_wasm_memfs_input()). */
#define WASM_MEMFS_INPUT (-1)

static RemixEntry *g_wasm_entries = NULL;
static unsigned g_wasm_entry_count;
static unsigned g_wasm_entry_cap;
static int g_wasm_convert_st16 = 1;
static int g_wasm_convert_shpx = 0;
static char g_wasm_mix_basename[256];
static const char g_wasm_w16_dir[] = ".";

static int write_file(const char *path, const uint8_t *data, size_t len)
{
	FILE *f = fopen(path, "wb");
	if (!f)
		return 0;
	if (len > 0 && fwrite(data, 1, len, f) != len) {
		fclose(f);
		return 0;
	}
	return fclose(f) == 0;
}

static uint8_t *read_file(const char *path, size_t *out_len)
{
	FILE *f = fopen(path, "rb");
	long n;
	uint8_t *buf;

	if (!f)
		return NULL;
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return NULL;
	}
	n = ftell(f);
	if (n < 0) {
		fclose(f);
		return NULL;
	}
	if (fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return NULL;
	}
	buf = (uint8_t *)malloc((size_t)n);
	if (!buf) {
		fclose(f);
		return NULL;
	}
	if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) {
		free(buf);
		fclose(f);
		return NULL;
	}
	fclose(f);
	*out_len = (size_t)n;
	return buf;
}

static void wasm_workfiles_cleanup(void)
{
	remove(WASM_IN_PATH);
	remove(WASM_IN_B_PATH);
	remove(WASM_OUT_PATH);
	remove(WASM_MERGE_TMP_PATH);
}

static void wasm_prepare_outputs(void)
{
	remove(WASM_OUT_PATH);
	remove(WASM_MERGE_TMP_PATH);
}

static void wasm_entries_reset(void)
{
	free(g_wasm_entries);
	g_wasm_entries = NULL;
	g_wasm_entry_count = 0;
	g_wasm_entry_cap = 0;
}

static void wasm_entry_report(const RemixEntry *entry, void *ctx)
{
	RemixEntry *next;

	(void)ctx;
	if (!entry)
		return;

	if (g_wasm_entry_count >= g_wasm_entry_cap) {
		unsigned new_cap = g_wasm_entry_cap ? g_wasm_entry_cap * 2u : 256u;
		next = (RemixEntry *)realloc(g_wasm_entries, (size_t)new_cap * sizeof(RemixEntry));
		if (!next)
			return;
		g_wasm_entries = next;
		g_wasm_entry_cap = new_cap;
	}
	g_wasm_entries[g_wasm_entry_count++] = *entry;
}

static void wasm_config_init(RemixConfig *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->ui = REMIX_UI_WASM;
	cfg->fallback_copy_on_convert_fail = 1;
	cfg->convert_st16_iconsets = g_wasm_convert_st16;
	cfg->convert_shpx = g_wasm_convert_shpx;
	cfg->shpx_pool_id = REMIX_SHPX_POOL_ID_DEFAULT;
	cfg->mix_basename = g_wasm_mix_basename[0] ? g_wasm_mix_basename : NULL;
	cfg->w16_dir = g_wasm_convert_st16 ? g_wasm_w16_dir : NULL;
	cfg->entry_report = wasm_entry_report;
	cfg->entry_report_ctx = NULL;
}

static int wasm_stage_input(const uint8_t *in_data, int in_len, const char *path)
{
	if (in_len == WASM_MEMFS_INPUT)
		return 1;
	if (!in_data || in_len <= 0)
		return 0;
	remove(path);
	return write_file(path, in_data, (size_t)in_len);
}

EMSCRIPTEN_KEEPALIVE
int remix_wasm_memfs_input(void)
{
	return WASM_MEMFS_INPUT;
}

EMSCRIPTEN_KEEPALIVE
void remix_wasm_set_st16_enabled(int enabled)
{
	g_wasm_convert_st16 = enabled ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
void remix_wasm_set_shpx_enabled(int enabled)
{
	g_wasm_convert_shpx = enabled ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
void remix_wasm_set_mix_basename(const char *basename)
{
	if (!basename) {
		g_wasm_mix_basename[0] = '\0';
		return;
	}
	snprintf(g_wasm_mix_basename, sizeof(g_wasm_mix_basename), "%s", basename);
}

EMSCRIPTEN_KEEPALIVE
int remix_wasm_install_w16(const uint8_t *data, int len)
{
	if (!data || len <= 0)
		return 0;
	return remix_st16_install_weights_from_buffer(data, (size_t)len);
}

EMSCRIPTEN_KEEPALIVE
unsigned remix_wasm_last_entry_count(void)
{
	return g_wasm_entry_count;
}

EMSCRIPTEN_KEEPALIVE
RemixEntry *remix_wasm_last_entries(void)
{
	return g_wasm_entries;
}

EMSCRIPTEN_KEEPALIVE
int remix_wasm_process_mix(
    const uint8_t *in_data, int in_len, uint8_t **out_data, int *out_len, RemixStats *stats)
{
	RemixConfig cfg;
	int rc;

	wasm_config_init(&cfg);
	wasm_entries_reset();

	if (stats)
		remix_stats_init(stats);

	wasm_prepare_outputs();
	if (!wasm_stage_input(in_data, in_len, WASM_IN_PATH))
		return 0;

	rc = remix_mix_file_ex(WASM_IN_PATH, WASM_OUT_PATH, &cfg, stats);
	if (rc <= 0)
		return rc;

	if (out_data && out_len) {
		*out_data = read_file(WASM_OUT_PATH, (size_t *)out_len);
		if (!*out_data)
			return 0;
		if (*out_len <= 0)
			return 0;
	}
	return 1;
}

EMSCRIPTEN_KEEPALIVE
int remix_wasm_merge_and_process_mix(
    const uint8_t *in_a, int len_a, const uint8_t *in_b, int len_b, uint8_t **out_data,
    int *out_len, RemixStats *stats)
{
	RemixConfig cfg;
	const char *paths[2];
	int rc;

	wasm_config_init(&cfg);
	wasm_entries_reset();

	if (stats)
		remix_stats_init(stats);

	wasm_prepare_outputs();
	if (!wasm_stage_input(in_a, len_a, WASM_IN_PATH))
		return 0;
	if (!wasm_stage_input(in_b, len_b, WASM_IN_B_PATH))
		return 0;

	paths[0] = WASM_IN_PATH;
	paths[1] = WASM_IN_B_PATH;
	rc = remix_mix_merge_and_repack(WASM_OUT_PATH, paths, 2, &cfg, stats);
	if (rc <= 0)
		return rc;

	if (out_data && out_len) {
		*out_data = read_file(WASM_OUT_PATH, (size_t *)out_len);
		if (!*out_data)
			return 0;
		if (*out_len <= 0)
			return 0;
	}
	return 1;
}

EMSCRIPTEN_KEEPALIVE
void remix_wasm_cleanup_workfiles(void)
{
	wasm_workfiles_cleanup();
}

EMSCRIPTEN_KEEPALIVE
void remix_wasm_free(void *ptr)
{
	free(ptr);
}
