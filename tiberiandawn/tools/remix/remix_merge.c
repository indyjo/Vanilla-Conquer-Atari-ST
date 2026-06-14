/*
 * remix_merge.c — union plain MIX index entries from multiple archives.
 *
 * Duplicate CRC + same size: keep first occurrence.
 * Duplicate CRC + different size: error.
 */

#include "remix.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct MergeSource {
	FILE *f;
	uint32_t data_start;
	RemixEntry *entries;
	uint16_t count;
} MergeSource;

typedef struct MergedItem {
	uint32_t crc;
	uint32_t src_offset;
	uint32_t size;
	unsigned src_index;
} MergedItem;

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

static int read_plain_mix(FILE *f, MergeSource *src)
{
	unsigned char hdr[6];
	long pos;
	unsigned i;

	if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr))
		return 0;

	src->count = read_le16(hdr);
	if (src->count == 0)
		return 0;

	pos = ftell(f);
	if (pos < 0)
		return 0;
	src->data_start = (uint32_t)pos + (uint32_t)src->count * 12u;
	if ((size_t)src->count * sizeof(RemixEntry) > REMIX_DIR_CACHE_MAX)
		return 0;

	src->entries = (RemixEntry *)calloc(src->count, sizeof(RemixEntry));
	if (!src->entries)
		return 0;

	for (i = 0; i < src->count; ++i) {
		unsigned char sb[12];
		if (fread(sb, 1, sizeof(sb), f) != sizeof(sb))
			return 0;
		src->entries[i].crc = read_le32(sb);
		src->entries[i].old_offset = read_le32(sb + 4);
		src->entries[i].old_size = read_le32(sb + 8);
	}
	return 1;
}

static void free_sources(MergeSource *src, unsigned count)
{
	unsigned i;

	for (i = 0; i < count; ++i) {
		free(src[i].entries);
		src[i].entries = NULL;
		if (src[i].f)
			fclose(src[i].f);
		src[i].f = NULL;
	}
}

static int merged_find(const MergedItem *items, unsigned count, uint32_t crc)
{
	unsigned i;

	for (i = 0; i < count; ++i) {
		if (items[i].crc == crc)
			return (int)i;
	}
	return -1;
}

static int merged_item_cmp(const void *a, const void *b)
{
	const MergedItem *ea = (const MergedItem *)a;
	const MergedItem *eb = (const MergedItem *)b;
	int32_t ca = (int32_t)ea->crc;
	int32_t cb = (int32_t)eb->crc;

	if (ca < cb)
		return -1;
	if (ca > cb)
		return 1;
	return 0;
}

static int copy_payload(FILE *in, FILE *out, uint32_t data_start, uint32_t offset, uint32_t size)
{
	unsigned char chunk[REMIX_COPY_CHUNK];
	uint32_t remaining = size;

	if (fseek(in, (long)data_start + (long)offset, SEEK_SET) != 0)
		return 0;

	while (remaining > 0) {
		size_t n = remaining > REMIX_COPY_CHUNK ? REMIX_COPY_CHUNK : remaining;
		if (fread(chunk, 1, n, in) != n)
			return 0;
		if (fwrite(chunk, 1, n, out) != n)
			return 0;
		remaining -= (uint32_t)n;
	}
	return 1;
}

static int write_merged_index(
    FILE *out, uint16_t count, uint32_t data_size, const MergedItem *items, uint32_t *out_offsets,
    const uint32_t *out_sizes)
{
	unsigned char hdr[6];
	unsigned char sb[12];
	MergedItem *sorted;
	unsigned i;

	sorted = (MergedItem *)malloc((size_t)count * sizeof(MergedItem));
	if (!sorted)
		return 0;
	memcpy(sorted, items, (size_t)count * sizeof(MergedItem));
	qsort(sorted, count, sizeof(MergedItem), merged_item_cmp);

	if (fseek(out, 0, SEEK_SET) != 0)
		goto fail;

	write_le16(hdr, count);
	write_le32(hdr + 2, data_size);
	if (fwrite(hdr, 1, sizeof(hdr), out) != sizeof(hdr))
		goto fail;

	for (i = 0; i < count; ++i) {
		unsigned j;
		uint32_t crc = sorted[i].crc;
		uint32_t off = 0;
		uint32_t sz = 0;

		for (j = 0; j < count; ++j) {
			if (items[j].crc == crc) {
				off = out_offsets[j];
				sz = out_sizes[j];
				break;
			}
		}

		write_le32(sb, crc);
		write_le32(sb + 4, off);
		write_le32(sb + 8, sz);
		if (fwrite(sb, 1, sizeof(sb), out) != sizeof(sb))
			goto fail;
	}

	free(sorted);
	return 1;

fail:
	free(sorted);
	return 0;
}

int remix_mix_merge(const char *out_path, const char **in_paths, unsigned in_count)
{
	MergeSource *sources = NULL;
	MergedItem *merged = NULL;
	uint32_t *new_offsets = NULL;
	uint32_t *new_sizes = NULL;
	FILE *out = NULL;
	unsigned merged_count = 0;
	unsigned i;
	unsigned j;
	uint32_t body_pos = 0;
	int rc = 0;

	if (!out_path || !in_paths || in_count == 0)
		return 0;

	sources = (MergeSource *)calloc(in_count, sizeof(MergeSource));
	if (!sources)
		return 0;

	for (i = 0; i < in_count; ++i) {
		sources[i].f = fopen(in_paths[i], "rb");
		if (!sources[i].f) {
			free_sources(sources, in_count);
			free(sources);
			return 0;
		}
		if (!read_plain_mix(sources[i].f, &sources[i])) {
			free_sources(sources, in_count);
			free(sources);
			return -1;
		}
	}

	for (i = 0; i < in_count; ++i) {
		unsigned k;
		for (k = 0; k < sources[i].count; ++k) {
			RemixEntry *e = &sources[i].entries[k];
			int existing;

			if (e->old_size == 0)
				continue;

			existing = merged_find(merged, merged_count, e->crc);
			if (existing >= 0) {
				if (merged[existing].size != e->old_size)
					goto fail;
				continue;
			}

			merged = (MergedItem *)realloc(merged, (merged_count + 1) * sizeof(MergedItem));
			if (!merged)
				goto fail;
			merged[merged_count].crc = e->crc;
			merged[merged_count].src_offset = e->old_offset;
			merged[merged_count].size = e->old_size;
			merged[merged_count].src_index = i;
			++merged_count;
		}
	}

	if (merged_count == 0)
		goto fail;

	new_offsets = (uint32_t *)calloc(merged_count, sizeof(uint32_t));
	new_sizes = (uint32_t *)calloc(merged_count, sizeof(uint32_t));
	if (!new_offsets || !new_sizes)
		goto fail;

	out = fopen(out_path, "wb");
	if (!out)
		goto fail;

	{
		unsigned char hdr[6];
		unsigned i2;

		write_le16(hdr, (uint16_t)merged_count);
		write_le32(hdr + 2, 0);
		if (fwrite(hdr, 1, sizeof(hdr), out) != sizeof(hdr))
			goto fail;
		for (i2 = 0; i2 < merged_count; ++i2) {
			unsigned char sb[12] = {0};
			if (fwrite(sb, 1, sizeof(sb), out) != sizeof(sb))
				goto fail;
		}
	}

	for (j = 0; j < merged_count; ++j) {
		MergedItem *item = &merged[j];
		MergeSource *src = &sources[item->src_index];

		new_offsets[j] = body_pos;
		if (!copy_payload(src->f, out, src->data_start, item->src_offset, item->size))
			goto fail;
		new_sizes[j] = item->size;
		body_pos += item->size;
	}

	if (!write_merged_index(out, (uint16_t)merged_count, body_pos, merged, new_offsets, new_sizes))
		goto fail;

	rc = 1;

fail:
	if (out)
		fclose(out);
	free(new_offsets);
	free(new_sizes);
	free(merged);
	free_sources(sources, in_count);
	free(sources);
	return rc;
}

int remix_mix_merge_and_repack(
    const char *out_path, const char **in_paths, unsigned in_count, const RemixConfig *cfg,
    RemixStats *stats)
{
	char temp_path[4096];
	int rc;

	if (!out_path || !in_paths || in_count == 0 || !cfg)
		return 0;

	if (in_count == 1)
		return remix_mix_file(in_paths[0], out_path, cfg, stats);

	if (snprintf(temp_path, sizeof(temp_path), "%s.merge.tmp", out_path) >= (int)sizeof(temp_path))
		return 0;

	rc = remix_mix_merge(temp_path, in_paths, in_count);
	if (rc <= 0) {
		remove(temp_path);
		return rc;
	}

	rc = remix_mix_file(temp_path, out_path, cfg, stats);
	remove(temp_path);
	return rc;
}
