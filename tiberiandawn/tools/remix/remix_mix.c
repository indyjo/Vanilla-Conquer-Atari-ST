#include "remix.h"

#include "remix_audio.h"
#include "remix_detect.h"
#include "remix_print.h"
#include "remix_shpx.h"
#include "remix_st16.h"
#include "remix_vqa.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t read_le16(const unsigned char *p)
{
	return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_le16(unsigned char *p, uint16_t v)
{
	p[0] = (unsigned char)(v & 0xFFu);
	p[1] = (unsigned char)((v >> 8) & 0xFFu);
}

static void write_le32(unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char)(v & 0xFFu);
	p[1] = (unsigned char)((v >> 8) & 0xFFu);
	p[2] = (unsigned char)((v >> 16) & 0xFFu);
	p[3] = (unsigned char)((v >> 24) & 0xFFu);
}

void remix_stats_init(RemixStats *stats)
{
	memset(stats, 0, sizeof(*stats));
}

static int read_plain_mix_header(FILE *f, RemixMix *mix)
{
	unsigned char hdr[6];
	long pos;

	if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr))
		return 0;

	mix->count = read_le16(hdr);
	mix->data_size = read_le32(hdr + 2);
	if (mix->count == 0)
		return 0;

	pos = ftell(f);
	if (pos < 0)
		return 0;
	mix->data_start = (uint32_t)pos + (uint32_t)mix->count * 12u;
	if ((size_t)mix->count * sizeof(RemixEntry) > REMIX_DIR_CACHE_MAX)
		return 0;

	mix->entries = (RemixEntry *)calloc(mix->count, sizeof(RemixEntry));
	if (!mix->entries)
		return 0;

	{
		unsigned i;
		for (i = 0; i < mix->count; ++i) {
			unsigned char sb[12];
			if (fread(sb, 1, sizeof(sb), f) != sizeof(sb))
				return 0;
			mix->entries[i].crc = read_le32(sb);
			mix->entries[i].old_offset = read_le32(sb + 4);
			mix->entries[i].old_size = read_le32(sb + 8);
		}
	}
	return 1;
}

static void free_mix(RemixMix *mix)
{
	free(mix->entries);
	mix->entries = NULL;
}

static int stream_copy(FILE *in, FILE *out, size_t nbytes, RemixProgressFn progress, void *progress_ctx)
{
	unsigned char chunk[REMIX_COPY_CHUNK];
	size_t total = nbytes;
	size_t done = 0;

	while (nbytes > 0) {
		size_t n = nbytes > REMIX_COPY_CHUNK ? REMIX_COPY_CHUNK : nbytes;
		if (fread(chunk, 1, n, in) != n)
			return 0;
		if (fwrite(chunk, 1, n, out) != n)
			return 0;
		done += n;
		nbytes -= n;
		if (progress)
			progress(progress_ctx, "COPY", (unsigned)done, (unsigned)total);
	}
	return 1;
}

static int entry_cmp_crc(const void *a, const void *b)
{
	const RemixEntry *ea = (const RemixEntry *)a;
	const RemixEntry *eb = (const RemixEntry *)b;
	/* Match MixFileClass::compfunc (signed int32 CRC compare). */
	int32_t ca = (int32_t)ea->crc;
	int32_t cb = (int32_t)eb->crc;

	if (ca < cb)
		return -1;
	if (ca > cb)
		return 1;
	return 0;
}

static int write_mix_index(FILE *out, const RemixMix *mix, int final_offsets)
{
	unsigned char hdr[6];
	unsigned char sb[12];
	RemixEntry *sorted;
	unsigned i;
	unsigned out_count = 0;

	for (i = 0; i < mix->count; ++i) {
		if (!mix->entries[i].omit)
			++out_count;
	}
	if (out_count == 0)
		return 0;
	sorted = (RemixEntry *)malloc((size_t)out_count * sizeof(RemixEntry));
	if (!sorted)
		return 0;
	out_count = 0;
	for (i = 0; i < mix->count; ++i) {
		if (mix->entries[i].omit)
			continue;
		sorted[out_count++] = mix->entries[i];
	}
	qsort(sorted, out_count, sizeof(RemixEntry), entry_cmp_crc);

	if (fseek(out, 0, SEEK_SET) != 0) {
		free(sorted);
		return 0;
	}

	write_le16(hdr, (uint16_t)out_count);
	write_le32(hdr + 2, final_offsets ? mix->data_size : 0);
	if (fwrite(hdr, 1, sizeof(hdr), out) != sizeof(hdr))
		goto fail;

	for (i = 0; i < out_count; ++i) {
		const RemixEntry *e = &sorted[i];
		write_le32(sb, e->crc);
		if (final_offsets) {
			write_le32(sb + 4, e->new_offset);
			write_le32(sb + 8, e->new_size);
		} else {
			write_le32(sb + 4, 0);
			write_le32(sb + 8, 0);
		}
		if (fwrite(sb, 1, sizeof(sb), out) != sizeof(sb))
			goto fail;
	}
	free(sorted);
	return 1;

fail:
	free(sorted);
	return 0;
}

static int write_placeholder_header(FILE *out, const RemixMix *mix)
{
	return write_mix_index(out, mix, 0);
}

static int patch_header(FILE *out, const RemixMix *mix)
{
	return write_mix_index(out, mix, 1);
}

/** If any entries were omitted, rewrite header+body so data_start matches new count. */
static int finalize_mix_file(FILE *out, RemixMix *mix, uint32_t body_bytes, uint32_t orig_data_start)
{
	unsigned kept = 0;
	unsigned i;
	unsigned char *body = NULL;
	uint32_t new_data_start;

	for (i = 0; i < mix->count; ++i) {
		if (!mix->entries[i].omit)
			++kept;
	}
	mix->data_size = body_bytes;

	if (kept == mix->count)
		return patch_header(out, mix);

	if (kept == 0)
		return 0;

	body = (unsigned char *)malloc(body_bytes ? body_bytes : 1u);
	if (!body)
		return 0;
	if (body_bytes > 0) {
		if (fseek(out, (long)orig_data_start, SEEK_SET) != 0)
			goto fail;
		if (fread(body, 1, body_bytes, out) != body_bytes)
			goto fail;
	}

	new_data_start = 6u + kept * 12u;
	if (!patch_header(out, mix))
		goto fail;
	/* patch_header wrote kept entries; seek to new data start and write body. */
	if (fseek(out, (long)new_data_start, SEEK_SET) != 0)
		goto fail;
	if (body_bytes > 0 && fwrite(body, 1, body_bytes, out) != body_bytes)
		goto fail;
	free(body);
	body = NULL;
	/* Trailing bytes past the new EOF are harmless; MIX readers use header sizes. */

	/* Compact in-memory entry list for reporting. */
	{
		unsigned w = 0;
		for (i = 0; i < mix->count; ++i) {
			if (mix->entries[i].omit)
				continue;
			if (w != i)
				mix->entries[w] = mix->entries[i];
			++w;
		}
		mix->count = (uint16_t)w;
	}
	return 1;

fail:
	free(body);
	return 0;
}

typedef struct ProgressCtx {
	const RemixConfig *cfg;
} ProgressCtx;

static void mix_progress(void *ctx, const char *verb, unsigned done, unsigned total)
{
	(void)ctx;
	(void)verb;
	(void)done;
	(void)total;
}

static int copy_payload(
    FILE *in, FILE *out, const unsigned char *probe, size_t probe_len, uint32_t payload_size,
    RemixProgressFn progress, void *progress_ctx)
{
	if (fwrite(probe, 1, probe_len, out) != probe_len)
		return 0;
	if (probe_len < payload_size) {
		if (!stream_copy(in, out, payload_size - probe_len, progress, progress_ctx))
			return 0;
	} else if (progress) {
		progress(progress_ctx, "COPY", 1, 1);
	}
	return 1;
}

static int write_payload_buffer(
    FILE *out, const unsigned char *payload, uint32_t payload_size, RemixProgressFn progress,
    void *progress_ctx)
{
	size_t done = 0;
	size_t left = payload_size;
	const unsigned char *p = payload;

	while (left > 0) {
		size_t n = left > REMIX_COPY_CHUNK ? REMIX_COPY_CHUNK : left;
		if (fwrite(p, 1, n, out) != n)
			return 0;
		done += n;
		p += n;
		left -= n;
		if (progress)
			progress(progress_ctx, "CONVERT", (unsigned)done, (unsigned)payload_size);
	}
	return 1;
}

static int process_iconset_payload(
    FILE *in, FILE *out, long in_pos, const unsigned char *probe, size_t probe_len,
    uint32_t payload_size, RemixEntry *e, RemixStats *stats, RemixProgressFn progress,
    void *progress_ctx, int *out_converted)
{
	unsigned char *payload = NULL;
	size_t new_size;
	int is_native;
	int should_convert;

	(void)probe_len;
	*out_converted = 0;

	if (stats)
		++stats->iconset_files;

	is_native = remix_st16_is_native(probe, payload_size);
	should_convert = !is_native && remix_st16_should_convert(probe, payload_size);

	if (!is_native && !should_convert)
		return -1;

	payload = (unsigned char *)malloc(payload_size);
	if (!payload)
		return 0;

	if (fseek(in, in_pos, SEEK_SET) != 0)
		goto fail;
	if (fread(payload, 1, payload_size, in) != payload_size)
		goto fail;

	if (is_native) {
		if (stats)
			++stats->iconset_already_st16;
		snprintf(e->type_out, sizeof(e->type_out), "st16");
		if (!write_payload_buffer(out, payload, payload_size, progress, progress_ctx))
			goto fail;
		e->new_size = payload_size;
		free(payload);
		return 1;
	}

	new_size = payload_size;
	if (!remix_st16_convert_payload(payload, &new_size)) {
		if (stats)
			++stats->iconset_errors;
		free(payload);
		return 0;
	}

	snprintf(e->type_out, sizeof(e->type_out), "st16");
	if (!write_payload_buffer(out, payload, (uint32_t)new_size, progress, progress_ctx))
		goto fail;
	e->new_size = (uint32_t)new_size;
	*out_converted = 1;
	if (stats)
		++stats->iconset_converted;
	free(payload);
	return 1;

fail:
	free(payload);
	return 0;
}

static int process_shpx_payload(
    FILE *in, FILE *out, long in_pos, const unsigned char *probe, size_t probe_len,
    uint32_t payload_size, RemixEntry *e, const RemixConfig *cfg, RemixStats *stats,
    RemixShpxPool *pool, RemixProgressFn progress, void *progress_ctx, int *out_converted)
{
	unsigned char *payload = NULL;
	unsigned char *shpx = NULL;
	size_t shpx_len = 0;
	RemixShpxConvertOpts shpx_opts;
	int rc;

	(void)probe_len;
	*out_converted = 0;

	if (stats)
		++stats->shpx_files;

	if (!remix_is_keyframe_shp(probe, payload_size)) {
		if (stats)
			++stats->shpx_skipped;
		return -1;
	}

	payload = (unsigned char *)malloc(payload_size);
	if (!payload)
		return 0;

	if (fseek(in, in_pos, SEEK_SET) != 0)
		goto fail;
	if (fread(payload, 1, payload_size, in) != payload_size)
		goto fail;

	shpx_opts.verbose = (cfg && cfg->shpx_verbose) ? 1 : 0;
	shpx_opts.entry_crc = e->crc;
	rc = remix_shpx_convert(payload, payload_size, &shpx, &shpx_len, pool, &shpx_opts);
	if (rc < 0) {
		if (stats)
			++stats->shpx_skipped;
		free(payload);
		return -1;
	}
	if (rc == 0 || !shpx) {
		if (stats)
			++stats->shpx_errors;
		goto fail;
	}

	snprintf(e->type_out, sizeof(e->type_out), "shpx");
	if (!write_payload_buffer(out, shpx, (uint32_t)shpx_len, progress, progress_ctx))
		goto fail_fail;
	e->new_size = (uint32_t)shpx_len;
	*out_converted = 1;
	if (stats)
		++stats->shpx_converted;
	free(shpx);
	free(payload);
	return 1;

fail_fail:
	free(shpx);
fail:
	free(payload);
	return 0;
}

static int process_entry(
    FILE *in, FILE *out, RemixMix *mix, unsigned index, uint32_t *body_pos,
    const RemixConfig *cfg, RemixStats *stats, RemixShpxPool *shpx_pool)
{
	RemixEntry *e = &mix->entries[index];
	unsigned char probe[REMIX_PROBE_LEN];
	size_t probe_len;
	long in_pos;
	long out_payload_start;
	int is_audio;
	int needs_convert;
	int iconset_rc;
	int iconset_converted = 0;
	int try_iconset = 0;
	int shpx_rc;
	int shpx_converted = 0;
	int try_shpx = 0;
	int try_vqa = 0;
	int vqa_rc = -1;
	ProgressCtx progress;

	e->new_offset = 0;
	e->new_size = 0;
	e->omit = 0;
	snprintf(e->type_in, sizeof(e->type_in), "empty");
	snprintf(e->type_out, sizeof(e->type_out), "empty");

	if (e->old_size == 0) {
		if (cfg->entry_report)
			cfg->entry_report(e, cfg->entry_report_ctx);
		return 1;
	}

	in_pos = (long)mix->data_start + (long)e->old_offset;
	if (fseek(in, in_pos, SEEK_SET) != 0)
		return 0;

	probe_len = e->old_size < sizeof(probe) ? e->old_size : sizeof(probe);
	if (fread(probe, 1, probe_len, in) != probe_len)
		return 0;

	remix_detect_file_type(probe, probe_len, e->old_size, e->type_in, sizeof(e->type_in));
	snprintf(e->type_out, sizeof(e->type_out), "%s", e->type_in);

	is_audio = remix_is_audio_payload(probe, probe_len, e->old_size);
	needs_convert = is_audio && remix_aud_needs_convert(probe, probe_len, e->old_size);
	try_iconset = cfg && cfg->convert_st16_iconsets && cfg->mix_basename
	    && remix_st16_is_theater_mix(cfg->mix_basename) && !needs_convert;
	try_shpx = cfg && cfg->convert_shpx && cfg->mix_basename
	    && remix_shpx_is_eligible(cfg->mix_basename) && !needs_convert;
	try_vqa = cfg && cfg->convert_vqa && strcmp(e->type_in, "vqa") == 0 && !needs_convert;

	if (stats) {
		++stats->payload_files;
		if (is_audio) {
			++stats->audio_files;
			if (!needs_convert)
				++stats->audio_already_ok;
		}
		if (strcmp(e->type_in, "vqa") == 0 || strcmp(e->type_in, "stv") == 0) {
			++stats->vqa_files;
			if (strcmp(e->type_in, "stv") == 0)
				++stats->vqa_already_stv;
		}
	}

	if (((mix->data_start + *body_pos) & 1u) != 0u) {
		unsigned char pad = 0;
		if (fwrite(&pad, 1, 1, out) != 1)
			return 0;
		++(*body_pos);
	}

	e->new_offset = *body_pos;
	out_payload_start = ftell(out);
	if (out_payload_start < 0)
		return 0;

	memset(&progress, 0, sizeof(progress));
	progress.cfg = cfg;

	iconset_rc = -1;
	if (try_iconset) {
		iconset_rc = process_iconset_payload(
		    in, out, in_pos, probe, probe_len, e->old_size, e, stats, mix_progress,
		    &progress, &iconset_converted);
		if (iconset_rc == 0)
			return 0;
	}

	shpx_rc = -1;
	if (iconset_rc <= 0 && try_shpx && shpx_pool) {
		shpx_rc = process_shpx_payload(
		    in, out, in_pos, probe, probe_len, e->old_size, e, cfg, stats, shpx_pool,
		    mix_progress, &progress, &shpx_converted);
		if (shpx_rc == 0) {
			if (cfg->fallback_copy_on_convert_fail) {
				char warn[REMIX_LINE_WIDTH + 1];

				snprintf(
				    warn, sizeof(warn), "WARN %08X shpx copy", (unsigned)e->crc);
				if (cfg->ui == REMIX_UI_HOST)
					remix_print_st_warn(warn);
				if (fseek(out, out_payload_start, SEEK_SET) != 0)
					return 0;
				*body_pos = e->new_offset;
				if (fseek(in, in_pos, SEEK_SET) != 0)
					return 0;
				if (!copy_payload(
				        in, out, probe, probe_len, e->old_size, mix_progress, &progress))
					return 0;
				e->new_size = e->old_size;
				snprintf(e->type_out, sizeof(e->type_out), "%s", e->type_in);
				if (stats)
					++stats->shpx_errors;
				shpx_rc = -1;
			} else {
				return 0;
			}
		}
	}

	if (iconset_rc <= 0 && shpx_rc <= 0 && try_vqa) {
		uint32_t out_payload = 0;

		vqa_rc = remix_vqa_convert_payload(
		    in, in_pos, probe, probe_len, e->old_size, e->crc, cfg, out, &out_payload);
		if (vqa_rc == 1) {
			e->new_size = out_payload;
			snprintf(e->type_out, sizeof(e->type_out), "stv");
			if (stats)
				++stats->vqa_converted;
		} else if (vqa_rc < 0) {
			/* Omit: rewind any pad written for this entry. */
			e->omit = 1;
			e->new_size = 0;
			snprintf(e->type_out, sizeof(e->type_out), "omit");
			if (fseek(out, out_payload_start, SEEK_SET) != 0)
				return 0;
			*body_pos = e->new_offset;
			/* If we padded before this entry, leave the pad (even alignment for next). */
			if (stats)
				++stats->vqa_omitted;
			if (cfg->ui == REMIX_UI_HOST)
				remix_print_host_entry(e);
			if (cfg->entry_report)
				cfg->entry_report(e, cfg->entry_report_ctx);
			(void)iconset_converted;
			(void)shpx_converted;
			return 1;
		} else {
			return 0;
		}
	} else if (iconset_rc <= 0 && shpx_rc <= 0 && needs_convert) {
		uint32_t out_payload = 0;
		int ok;

		ok = remix_audio_stream_convert(
		    probe, probe_len, e->old_size, NULL, NULL, in_pos, in, out,
		    mix_progress, &progress, &out_payload);
		if (ok) {
			e->new_size = out_payload;
			remix_format_aud_pcm_target(e->type_out, sizeof(e->type_out));
			if (stats)
				++stats->audio_converted;
		} else if (cfg->fallback_copy_on_convert_fail) {
			char warn[REMIX_LINE_WIDTH + 1];

			snprintf(
			    warn, sizeof(warn), "WARN %08X %s copy", (unsigned)e->crc,
			    remix_aud_fail_hint(probe, probe_len, e->old_size));
			if (cfg->ui == REMIX_UI_HOST)
				remix_print_st_warn(warn);
			if (fseek(out, out_payload_start, SEEK_SET) != 0)
				return 0;
			*body_pos = e->new_offset;
			if (fseek(in, in_pos, SEEK_SET) != 0)
				return 0;
			if (!copy_payload(in, out, probe, probe_len, e->old_size, mix_progress, &progress))
				return 0;
			e->new_size = e->old_size;
			if (stats)
				++stats->payload_errors;
		} else {
			return 0;
		}
	} else if (iconset_rc <= 0 && shpx_rc <= 0 && vqa_rc <= 0) {
		if (!copy_payload(in, out, probe, probe_len, e->old_size, mix_progress, &progress))
			return 0;
		e->new_size = e->old_size;
	}

	*body_pos += e->new_size;

	if (cfg->ui == REMIX_UI_HOST)
		remix_print_host_entry(e);

	if (cfg->entry_report)
		cfg->entry_report(e, cfg->entry_report_ctx);

	(void)iconset_converted;
	(void)shpx_converted;
	return 1;
}

int remix_mix_file_ex(const char *in_path, const char *out_path, const RemixConfig *cfg, RemixStats *stats)
{
	FILE *in = NULL;
	FILE *out = NULL;
	RemixMix mix;
	RemixConfig active_cfg;
	RemixShpxPool shpx_pool;
	char mix_base[256];
	uint32_t body_pos = 0;
	unsigned i;
	const RemixConfig *use_cfg = cfg;

	memset(&mix, 0, sizeof(mix));

	if (cfg) {
		active_cfg = *cfg;
	} else {
		memset(&active_cfg, 0, sizeof(active_cfg));
	}
	remix_path_basename(in_path, mix_base, sizeof(mix_base));
	if (!active_cfg.mix_basename)
		active_cfg.mix_basename = mix_base;
	if (active_cfg.shpx_pool_id == 0) {
		uint16_t from_name = remix_shpx_default_pool_id(active_cfg.mix_basename);

		active_cfg.shpx_pool_id = from_name ? from_name : REMIX_SHPX_POOL_ID_DEFAULT;
	}
	use_cfg = &active_cfg;

	remix_shpx_pool_init(&shpx_pool, use_cfg->shpx_pool_id);

	in = fopen(in_path, "rb");
	if (!in) {
		remix_shpx_pool_free(&shpx_pool);
		return 0;
	}

	if (!read_plain_mix_header(in, &mix)) {
		fclose(in);
		free_mix(&mix);
		remix_shpx_pool_free(&shpx_pool);
		return -1;
	}

	out = fopen(out_path, "wb");
	if (!out) {
		fclose(in);
		free_mix(&mix);
		remix_shpx_pool_free(&shpx_pool);
		return 0;
	}

	if (!write_placeholder_header(out, &mix))
		goto fail;

	if (!remix_st16_prepare_theater_mix(
		use_cfg->convert_st16_iconsets, use_cfg->mix_basename, use_cfg->w16_dir))
		goto fail;

	if (use_cfg->ui == REMIX_UI_HOST)
		remix_print_host_banner(in_path, out_path, mix.count, mix.data_start);

	if (use_cfg->ui == REMIX_UI_HOST)
		remix_print_host_table_header();

	for (i = 0; i < mix.count; ++i) {
		if (!process_entry(in, out, &mix, i, &body_pos, use_cfg, stats, &shpx_pool))
			goto fail;
	}

	if (!finalize_mix_file(out, &mix, body_pos, mix.data_start))
		goto fail;

	if (use_cfg->convert_shpx && shpx_pool.size > 0) {
		if (!remix_shpx_write_pool(&shpx_pool, out_path))
			goto fail;
	}

	fclose(in);
	fclose(out);
	remix_shpx_pool_free(&shpx_pool);

	if (use_cfg->ui == REMIX_UI_HOST)
		remix_print_host_mix_done(out_path, mix.count, mix.data_size);

	free_mix(&mix);
	if (stats)
		++stats->mix_files_ok;
	return 1;

fail:
	fclose(in);
	fclose(out);
	remix_shpx_pool_free(&shpx_pool);
	free_mix(&mix);
	return 0;
}

int remix_mix_file(const char *in_path, const char *out_path, const RemixConfig *cfg, RemixStats *stats)
{
	return remix_mix_file_ex(in_path, out_path, cfg, stats);
}

int remix_mix_file_inplace(const char *in_path, const char *temp_path, const RemixConfig *cfg, RemixStats *stats)
{
	int rc = remix_mix_file(in_path, temp_path, cfg, stats);

	if (rc <= 0)
		return rc;
	if (rename(temp_path, in_path) != 0) {
		remove(temp_path);
		return 0;
	}
	return 1;
}
