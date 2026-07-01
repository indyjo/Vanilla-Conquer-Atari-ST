#ifndef REMIX_H
#define REMIX_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "remix_aud.h"

enum {
	REMIX_TARGET_RATE = 11025,
	REMIX_PROBE_LEN = 512,
	REMIX_COPY_CHUNK = 16384,
	REMIX_COL_CRC = 8,
	REMIX_COL_SIZE = 7,
	REMIX_PROGRESS_DOTS = 32,
	REMIX_VERB_WIDTH = 7,
	REMIX_LINE_WIDTH = 40,
	REMIX_DIR_CACHE_MAX = 256 * 1024
};

#define REMIX_TEMP_MXX "temp.mxx"

typedef struct RemixEntry {
	uint32_t crc;
	uint32_t old_offset;
	uint32_t old_size;
	uint32_t new_offset;
	uint32_t new_size;
	char type_in[32];
	char type_out[32];
} RemixEntry;

typedef struct RemixMix {
	uint16_t count;
	uint32_t data_size;
	uint32_t data_start;
	RemixEntry *entries;
} RemixMix;

typedef struct RemixStats {
	unsigned mix_files_ok;
	unsigned mix_files_error;
	unsigned mix_files_skipped;
	unsigned payload_files;
	unsigned audio_files;
	unsigned audio_converted;
	unsigned audio_already_ok;
	unsigned payload_errors;
	unsigned iconset_files;
	unsigned iconset_converted;
	unsigned iconset_already_st16;
	unsigned iconset_errors;
	unsigned shpx_files;
	unsigned shpx_converted;
	unsigned shpx_skipped;
	unsigned shpx_errors;
} RemixStats;

typedef enum RemixUi {
	REMIX_UI_HOST,
	REMIX_UI_ST,
	REMIX_UI_WASM
} RemixUi;

typedef void (*RemixEntryReportFn)(const RemixEntry *entry, void *user_data);

typedef struct RemixConfig {
	RemixUi ui;
	int fallback_copy_on_convert_fail;
	int convert_st16_iconsets;
	int convert_shpx;
	int shpx_verbose;
	uint16_t shpx_pool_id;
	const char *w16_dir;
	const char *mix_basename;
	RemixEntryReportFn entry_report;
	void *entry_report_ctx;
} RemixConfig;

void remix_stats_init(RemixStats *stats);

int remix_mix_file(
    const char *in_path, const char *out_path, const RemixConfig *cfg, RemixStats *stats);

/** Same as remix_mix_file; name documents optional entry_report in cfg. */
int remix_mix_file_ex(
    const char *in_path, const char *out_path, const RemixConfig *cfg, RemixStats *stats);

int remix_mix_file_inplace(
    const char *in_path, const char *temp_path, const RemixConfig *cfg, RemixStats *stats);

/** Union index entries from in_paths[0..in_count-1]. Returns 1 ok, 0 error, -1 bad MIX. */
int remix_mix_merge(const char *out_path, const char **in_paths, unsigned in_count);

/** Merge (if in_count > 1) then repack to out_path. */
int remix_mix_merge_and_repack(
    const char *out_path, const char **in_paths, unsigned in_count, const RemixConfig *cfg,
    RemixStats *stats);

#endif /* REMIX_H */
