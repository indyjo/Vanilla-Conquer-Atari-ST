#include "remix.h"

#include "remix_audio.h"
#include "remix_detect.h"
#include "remix_print.h"

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

	if (mix->count == 0)
		return 0;
	sorted = (RemixEntry *)malloc((size_t)mix->count * sizeof(RemixEntry));
	if (!sorted)
		return 0;
	memcpy(sorted, mix->entries, (size_t)mix->count * sizeof(RemixEntry));
	qsort(sorted, mix->count, sizeof(RemixEntry), entry_cmp_crc);

	if (fseek(out, 0, SEEK_SET) != 0) {
		free(sorted);
		return 0;
	}

	write_le16(hdr, mix->count);
	write_le32(hdr + 2, final_offsets ? mix->data_size : 0);
	if (fwrite(hdr, 1, sizeof(hdr), out) != sizeof(hdr))
		goto fail;

	for (i = 0; i < mix->count; ++i) {
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

typedef struct ProgressCtx {
	const RemixConfig *cfg;
} ProgressCtx;

static void mix_progress(void *ctx, const char *verb, unsigned done, unsigned total)
{
	ProgressCtx *p = (ProgressCtx *)ctx;

	if (!p || p->cfg->ui != REMIX_UI_ST)
		return;
	remix_print_st_progress(verb, done, total);
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

static int process_entry(
    FILE *in, FILE *out, RemixMix *mix, unsigned index, uint32_t *body_pos,
    const RemixConfig *cfg, RemixStats *stats)
{
	RemixEntry *e = &mix->entries[index];
	unsigned char probe[REMIX_PROBE_LEN];
	size_t probe_len;
	long in_pos;
	long out_payload_start;
	int is_audio;
	int needs_convert;
	int converted = 0;
	ProgressCtx progress;

	e->new_offset = 0;
	e->new_size = 0;
	snprintf(e->type_in, sizeof(e->type_in), "empty");
	snprintf(e->type_out, sizeof(e->type_out), "empty");

	if (e->old_size == 0)
		return 1;

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

	if (stats) {
		++stats->payload_files;
		if (is_audio) {
			++stats->audio_files;
			if (!needs_convert)
				++stats->audio_already_ok;
		}
	}

	if (cfg->ui == REMIX_UI_ST)
		remix_print_st_entry_line(e->crc, e->old_size, e->type_in, e->type_in);

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
	if (cfg->ui == REMIX_UI_ST) {
		unsigned total = e->old_size > 0 ? (unsigned)e->old_size : 1u;

		remix_print_st_progress_reset();
		remix_print_st_progress(needs_convert ? "CONVERT" : "COPY", 0, total);
	}

	if (needs_convert) {
		uint32_t out_payload = 0;
		int ok;

		ok = remix_audio_stream_convert(
		    probe, probe_len, e->old_size, NULL, NULL, in_pos, in, out,
		    mix_progress, &progress, &out_payload);
		if (ok) {
			e->new_size = out_payload;
			remix_format_aud_pcm_target(e->type_out, sizeof(e->type_out));
			converted = 1;
			if (stats)
				++stats->audio_converted;
		} else if (cfg->fallback_copy_on_convert_fail) {
			char warn[REMIX_LINE_WIDTH + 1];
			unsigned total = e->old_size > 0 ? (unsigned)e->old_size : 1u;

			snprintf(
			    warn, sizeof(warn), "WARN %08X %s copy", (unsigned)e->crc,
			    remix_aud_fail_hint(probe, probe_len, e->old_size));
			if (cfg->ui == REMIX_UI_ST) {
				remix_print_st_progress_reset();
				remix_print_st_warn(warn);
				remix_print_st_progress("COPY", 0, total);
			} else {
				remix_print_st_warn(warn);
			}
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
	} else {
		if (!copy_payload(in, out, probe, probe_len, e->old_size, mix_progress, &progress))
			return 0;
		e->new_size = e->old_size;
	}

	*body_pos += e->new_size;

	if (cfg->ui == REMIX_UI_HOST)
		remix_print_host_entry(e);

	(void)converted;
	return 1;
}

int remix_mix_file(const char *in_path, const char *out_path, const RemixConfig *cfg, RemixStats *stats)
{
	FILE *in = NULL;
	FILE *out = NULL;
	RemixMix mix;
	uint32_t body_pos = 0;
	unsigned i;

	memset(&mix, 0, sizeof(mix));

	in = fopen(in_path, "rb");
	if (!in)
		return 0;

	if (!read_plain_mix_header(in, &mix)) {
		fclose(in);
		free_mix(&mix);
		return -1;
	}

	out = fopen(out_path, "wb");
	if (!out) {
		fclose(in);
		free_mix(&mix);
		return 0;
	}

	if (!write_placeholder_header(out, &mix))
		goto fail;

	if (cfg->ui == REMIX_UI_ST)
		remix_print_st_mix_header(in_path, mix.count);
	else
		remix_print_host_banner(in_path, out_path, mix.count, mix.data_start);

	if (cfg->ui == REMIX_UI_HOST)
		remix_print_host_table_header();

	for (i = 0; i < mix.count; ++i) {
		if (!process_entry(in, out, &mix, i, &body_pos, cfg, stats))
			goto fail;
	}

	mix.data_size = body_pos;
	if (!patch_header(out, &mix))
		goto fail;

	fclose(in);
	fclose(out);

	if (cfg->ui == REMIX_UI_ST)
		remix_print_st_mix_done(in_path, mix.count, mix.data_size);
	else if (cfg->ui == REMIX_UI_HOST)
		remix_print_host_mix_done(out_path, mix.count, mix.data_size);

	free_mix(&mix);
	if (stats)
		++stats->mix_files_ok;
	return 1;

fail:
	fclose(in);
	fclose(out);
	free_mix(&mix);
	return 0;
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
