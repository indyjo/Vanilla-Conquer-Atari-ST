/*
 * remix_audx.c — Convert PCM AUD entries to AUDX + pool sidecar.
 */

#include "remix_audx.h"

#include "remix_aud.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static uint16_t read_le16(const unsigned char *p)
{
	return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_be16(unsigned char *p, uint16_t v)
{
	p[0] = (unsigned char)((v >> 8) & 0xFFu);
	p[1] = (unsigned char)(v & 0xFFu);
}

static void write_be32(unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char)((v >> 24) & 0xFFu);
	p[1] = (unsigned char)((v >> 16) & 0xFFu);
	p[2] = (unsigned char)((v >> 8) & 0xFFu);
	p[3] = (unsigned char)(v & 0xFFu);
}

uint16_t remix_audx_default_pool_id(const char *mix_basename)
{
	if (!mix_basename)
		return 0;
	if (strcasecmp(mix_basename, "SOUNDS.MIX") == 0)
		return 0x0005u;
	if (strcasecmp(mix_basename, "SPEECH.MIX") == 0)
		return 0x0006u;
	if (strcasecmp(mix_basename, "SCORES.MIX") == 0)
		return 0x0007u;
	return 0;
}

int remix_audx_is_eligible(const char *mix_basename)
{
	return remix_audx_default_pool_id(mix_basename) != 0;
}

/*
 * SCORES entries to drop from the remixed MIX (Theme_File_Name falls back to .AUD).
 * AOI.VAR: odd/unreliable IMA header historically left a multi-MB classic AUD in SCORES.
 */
static const uint32_t k_scores_omit_crcs[] = {
	0x5CE4DFD8u, /* AOI.VAR */
};

int remix_audx_should_omit_entry(const char *mix_basename, uint32_t crc)
{
	size_t i;

	if (!mix_basename || strcasecmp(mix_basename, "SCORES.MIX") != 0)
		return 0;
	for (i = 0; i < sizeof(k_scores_omit_crcs) / sizeof(k_scores_omit_crcs[0]); ++i) {
		if (k_scores_omit_crcs[i] == crc)
			return 1;
	}
	return 0;
}

int remix_audx_format_pool_name(uint16_t pool_id, char *out, size_t out_cap)
{
	if (!out || out_cap < 13u || pool_id == 0)
		return 0;
	if (snprintf(out, out_cap, "pool%04x.bin", (unsigned)pool_id) >= (int)out_cap)
		return 0;
	return 1;
}

void remix_audx_pool_init(RemixAudxPool *pool, uint16_t pool_id)
{
	if (!pool)
		return;
	memset(pool, 0, sizeof(*pool));
	pool->pool_id = pool_id;
}

void remix_audx_pool_free(RemixAudxPool *pool)
{
	if (!pool)
		return;
	free(pool->data);
	pool->data = NULL;
	pool->size = 0;
	pool->cap = 0;
}

static int pool_reserve(RemixAudxPool *pool, size_t need)
{
	uint8_t *nbuf;
	size_t ncap;

	if (pool->size + need <= pool->cap)
		return 1;
	ncap = pool->cap ? pool->cap : 65536u;
	while (ncap < pool->size + need)
		ncap *= 2u;
	nbuf = (uint8_t *)realloc(pool->data, ncap);
	if (!nbuf)
		return 0;
	pool->data = nbuf;
	pool->cap = ncap;
	return 1;
}

int remix_audx_convert(
    const unsigned char *in, size_t in_len, unsigned char **out_buf, size_t *out_len, RemixAudxPool *pool)
{
	uint16_t rate;
	uint32_t size;
	uint32_t uncomp;
	uint8_t flags;
	uint8_t compression;
	const unsigned char *payload;
	size_t payload_len;
	uint32_t pool_begin;
	uint32_t pool_span;
	unsigned char *meta;

	if (!in || !out_buf || !out_len || !pool || pool->pool_id == 0)
		return 0;
	*out_buf = NULL;
	*out_len = 0;

	if (in_len < (size_t)REMIX_AUD_HDR_LEN)
		return -1;
	compression = in[11];
	if (compression != REMIX_AUD_COMP_PCM)
		return -1;

	rate = read_le16(in);
	size = read_le32(in + 2);
	uncomp = read_le32(in + 6);
	flags = in[10];
	payload = in + REMIX_AUD_HDR_LEN;
	payload_len = in_len - (size_t)REMIX_AUD_HDR_LEN;
	if (size > 0 && (size_t)size < payload_len)
		payload_len = (size_t)size;
	if (payload_len == 0)
		return 0;
	if (uncomp == 0)
		uncomp = (uint32_t)payload_len;

	/* Even-align pool begin. */
	if ((pool->size & 1u) != 0u) {
		if (!pool_reserve(pool, 1))
			return 0;
		pool->data[pool->size++] = 0;
	}
	pool_begin = (uint32_t)pool->size;
	pool_span = (uint32_t)payload_len;
	if ((pool_span & 1u) != 0u)
		pool_span += 1u;

	if (!pool_reserve(pool, pool_span))
		return 0;
	memcpy(pool->data + pool->size, payload, payload_len);
	if (pool_span > payload_len)
		pool->data[pool->size + payload_len] = 0;
	pool->size += pool_span;

	meta = (unsigned char *)malloc(REMIX_AUDX_PREFIX_SIZE);
	if (!meta)
		return 0;
	write_be32(meta + 0, REMIX_AUDX_MAGIC_BE);
	write_be16(meta + 4, rate);
	meta[6] = flags;
	meta[7] = compression;
	write_be32(meta + 8, (uint32_t)payload_len);
	write_be32(meta + 12, uncomp);
	write_be16(meta + 16, pool->pool_id);
	write_be16(meta + 18, 0);
	write_be32(meta + 20, pool_begin);
	write_be32(meta + 24, pool_span);

	*out_buf = meta;
	*out_len = REMIX_AUDX_PREFIX_SIZE;
	return 1;
}

int remix_audx_write_pool(const RemixAudxPool *pool, const char *out_mix_path)
{
	char dir[PATH_MAX];
	char path[PATH_MAX];
	char name[16];
	FILE *fp;
	const char *slash;
	size_t dir_len;

	if (!pool || !out_mix_path || pool->size == 0 || pool->pool_id == 0)
		return 0;
	if (!remix_audx_format_pool_name(pool->pool_id, name, sizeof(name)))
		return 0;

	slash = strrchr(out_mix_path, '/');
#ifdef _WIN32
	{
		const char *b = strrchr(out_mix_path, '\\');
		if (!slash || (b && b > slash))
			slash = b;
	}
#endif
	if (slash) {
		dir_len = (size_t)(slash - out_mix_path);
		if (dir_len + 1 >= sizeof(dir))
			return 0;
		memcpy(dir, out_mix_path, dir_len);
		dir[dir_len] = '\0';
		if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path))
			return 0;
	} else {
		if (snprintf(path, sizeof(path), "%s", name) >= (int)sizeof(path))
			return 0;
	}

	fp = fopen(path, "wb");
	if (!fp)
		return 0;
	if (fwrite(pool->data, 1, pool->size, fp) != pool->size) {
		fclose(fp);
		return 0;
	}
	fclose(fp);
	return 1;
}
