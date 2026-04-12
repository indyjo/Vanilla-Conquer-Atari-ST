/*
 * Standalone MIX index reader (matches MixFileClass index layout + Calculate_CRC).
 */

#include "st_mix_minimal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef BIG_ENDIAN
static inline short st_mix_swap16(short v)
{
	return (short)(((unsigned short)(v) << 8) | ((unsigned short)(v) >> 8));
}
static inline long st_mix_swap32(long v)
{
	unsigned long u = (unsigned long)v;
	return (long)((u << 24) | ((u & 0xFF00UL) << 8) | ((u & 0xFF0000UL) >> 8) | (u >> 24));
}
#else
static inline short st_mix_swap16(short v) { return v; }
static inline long st_mix_swap32(long v) { return v; }
#endif

#pragma pack(push, 1)
struct StMixFileHeader {
	short count;
	long size;
};
struct StMixSubBlock {
	long crc;
	long offset;
	long size;
};
#pragma pack(pop)

/* Same algorithm as ATARILIB/misc.cpp Calculate_CRC (Westwood MIX keys). */
static long st_mix_crc(void *buffer, long length)
{
	if (!buffer || length <= 0)
		return 0;

	unsigned long crc = 0;
	unsigned char *data = (unsigned char *)buffer;
	unsigned long local_length = (unsigned long)length;
	unsigned long num_chunks = (local_length + 3) >> 2;

	for (unsigned long i = 0; i < num_chunks; i++) {
		unsigned char *chunk_ptr = data + (i * 4);
		unsigned long bytes_available = local_length - (i * 4);
		unsigned long value = ((bytes_available > 0) ? (unsigned long)chunk_ptr[0] : 0UL) |
			((bytes_available > 1) ? ((unsigned long)chunk_ptr[1] << 8) : 0UL) |
			((bytes_available > 2) ? ((unsigned long)chunk_ptr[2] << 16) : 0UL) |
			((bytes_available > 3) ? ((unsigned long)chunk_ptr[3] << 24) : 0UL);
		unsigned long high_bit = (crc & 0x80000000UL) ? 1UL : 0UL;
		crc = (crc << 1) | high_bit;
		crc += value;
	}
	return (long)crc;
}

static void st_mix_strupr(char *s)
{
	for (; *s; ++s) {
		if (*s >= 'a' && *s <= 'z')
			*s = (char)(*s - ('a' - 'A'));
	}
}

static int st_mix_comp_crc(const void *a, const void *b)
{
	long ca = ((const StMixSubBlock *)a)->crc;
	long cb = ((const StMixSubBlock *)b)->crc;
	if (ca < cb)
		return -1;
	if (ca > cb)
		return 1;
	return 0;
}

int st_mix_extract_file(const char *mix_path, const char *entry_name,
		unsigned char **out_data, size_t *out_size)
{
	if (!mix_path || !entry_name || !out_data || !out_size)
		return -1;
	*out_data = NULL;
	*out_size = 0;

	char namecopy[256];
	strncpy(namecopy, entry_name, sizeof(namecopy) - 1);
	namecopy[sizeof(namecopy) - 1] = '\0';
	st_mix_strupr(namecopy);
	long key_crc = st_mix_crc(namecopy, (long)strlen(namecopy));

	FILE *f = fopen(mix_path, "rb");
	if (!f)
		return -2;

	StMixFileHeader fh;
	if (fread(&fh, 1, sizeof(fh), f) != sizeof(fh)) {
		fclose(f);
		return -3;
	}
	int count = (int)st_mix_swap16(fh.count);
	long data_size = st_mix_swap32(fh.size);
	if (count <= 0 || count > 1000000 || data_size < 0) {
		fclose(f);
		return -4;
	}

	size_t index_bytes = (size_t)count * sizeof(StMixSubBlock);
	StMixSubBlock *blocks = (StMixSubBlock *)malloc(index_bytes);
	if (!blocks) {
		fclose(f);
		return -5;
	}
	if (fread(blocks, 1, index_bytes, f) != index_bytes) {
		free(blocks);
		fclose(f);
		return -6;
	}

	for (int i = 0; i < count; i++) {
		blocks[i].crc = st_mix_swap32(blocks[i].crc);
		blocks[i].offset = st_mix_swap32(blocks[i].offset);
		blocks[i].size = st_mix_swap32(blocks[i].size);
	}

	StMixSubBlock key;
	key.crc = key_crc;
	key.offset = 0;
	key.size = 0;
	StMixSubBlock *hit = (StMixSubBlock *)bsearch(&key, blocks, (size_t)count, sizeof(StMixSubBlock), st_mix_comp_crc);
	if (!hit || hit->size <= 0) {
		free(blocks);
		fclose(f);
		return -7;
	}

	long data_base = (long)sizeof(StMixFileHeader) + (long)index_bytes;
	if (hit->offset < 0 || hit->offset > data_size || hit->size > data_size - hit->offset) {
		free(blocks);
		fclose(f);
		return -8;
	}

	unsigned char *buf = (unsigned char *)malloc((size_t)hit->size);
	if (!buf) {
		free(blocks);
		fclose(f);
		return -9;
	}

	if (fseek(f, data_base + hit->offset, SEEK_SET) != 0) {
		free(buf);
		free(blocks);
		fclose(f);
		return -10;
	}
	if (fread(buf, 1, (size_t)hit->size, f) != (size_t)hit->size) {
		free(buf);
		free(blocks);
		fclose(f);
		return -11;
	}

	free(blocks);
	fclose(f);
	*out_data = buf;
	*out_size = (size_t)hit->size;
	return 0;
}
