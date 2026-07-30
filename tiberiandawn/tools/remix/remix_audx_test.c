/*
 * Host unit test for remix_audx_convert.
 */
#include "remix_audx.h"
#include "remix_aud.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static uint32_t read_be32(const unsigned char *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t read_be16(const unsigned char *p)
{
	return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

int main(void)
{
	unsigned char aud[REMIX_AUD_HDR_LEN + 16];
	unsigned char *meta = NULL;
	size_t meta_len = 0;
	RemixAudxPool pool;
	int rc;
	int fail = 0;

	memset(aud, 0, sizeof(aud));
	write_le16(aud, 11025);
	write_le32(aud + 2, 16);
	write_le32(aud + 6, 16);
	aud[10] = 0;
	aud[11] = REMIX_AUD_COMP_PCM;
	memcpy(aud + REMIX_AUD_HDR_LEN, "0123456789ABCDEF", 16);

	remix_audx_pool_init(&pool, 5);
	rc = remix_audx_convert(aud, sizeof(aud), &meta, &meta_len, &pool);
	if (rc != 1 || !meta || meta_len != REMIX_AUDX_PREFIX_SIZE) {
		fprintf(stderr, "FAIL convert rc=%d meta_len=%zu\n", rc, meta_len);
		fail = 1;
	} else if (read_be32(meta) != REMIX_AUDX_MAGIC_BE) {
		fprintf(stderr, "FAIL magic\n");
		fail = 1;
	} else if (read_be16(meta + 4) != 11025) {
		fprintf(stderr, "FAIL rate\n");
		fail = 1;
	} else if (read_be32(meta + 8) != 16 || read_be32(meta + 12) != 16) {
		fprintf(stderr, "FAIL size/uncomp\n");
		fail = 1;
	} else if (read_be16(meta + 16) != 5) {
		fprintf(stderr, "FAIL pool_id\n");
		fail = 1;
	} else if (pool.size < 16 || memcmp(pool.data + read_be32(meta + 20), "0123456789ABCDEF", 16) != 0) {
		fprintf(stderr, "FAIL pool payload\n");
		fail = 1;
	}

	if (remix_audx_default_pool_id("SOUNDS.MIX") != 5 || remix_audx_default_pool_id("speech.mix") != 6
	    || remix_audx_default_pool_id("SCORES.MIX") != 7) {
		fprintf(stderr, "FAIL default pool ids\n");
		fail = 1;
	}

	if (!remix_audx_should_omit_entry("SCORES.MIX", 0x5CE4DFD8u)
	    || remix_audx_should_omit_entry("scores.mix", 0x5CE4DFD8u) == 0
	    || remix_audx_should_omit_entry("SOUNDS.MIX", 0x5CE4DFD8u)
	    || remix_audx_should_omit_entry("SCORES.MIX", 0x5CD6F3C3u)) {
		fprintf(stderr, "FAIL scores omit denylist\n");
		fail = 1;
	}

	free(meta);
	remix_audx_pool_free(&pool);
	if (fail) {
		fprintf(stderr, "remix_audx_test FAILED\n");
		return 1;
	}
	printf("remix_audx_test OK\n");
	return 0;
}
