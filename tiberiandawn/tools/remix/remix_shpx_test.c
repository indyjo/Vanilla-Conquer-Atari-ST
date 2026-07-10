/*
 * Host regression tests for remix SHPX / KeyFrame decode (run: make test-shpx).
 *
 * Curated shapes live in testdata/shapes/ (extract with testdata/extract_shapes.py).
 */

#include "keyframe.h"
#include "remix_shpx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	SHPX_PREFIX_SIZE = 38,
	SHPX_FRAME_SLOT_SIZE = 8
};

typedef struct {
	const char *filename;
	int expect_keyframe;
	int decode_all_frames;
	const unsigned short *spot_frames;
	int n_spot;
	int expect_shpx;
	int verify_tail_clip;
} ShpxTestCase;

static const unsigned short k_spot_minigun[] = { 0, 1, 2, 3, 4, 5 };
static const unsigned short k_spot_options[] = { 0, 2, 3 };
static const unsigned short k_spot_e4[] = { 0, 1, 15, 31, 63, 127, 255, 511 };
static const unsigned short k_spot_radar[] = { 0, 1, 2, 3, 4, 5, 30, 31, 32, 40, 41, 42 };
static const unsigned short k_spot_trex[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
static const unsigned short k_spot_nuke[] = { 7, 8 };

static const ShpxTestCase k_cases[] = {
	{ "50CAL.SHP", 1, 1, NULL, 0, 1 },
	{ "POWER.SHP", 1, 1, NULL, 0, 1 },
	{ "BOMB.SHP", 1, 1, NULL, 0, 1 },
	{ "MINIGUN.SHP", 1, 0, k_spot_minigun, (int)(sizeof(k_spot_minigun) / sizeof(k_spot_minigun[0])), 1 },
	{ "OPTIONS.SHP", 1, 0, k_spot_options, (int)(sizeof(k_spot_options) / sizeof(k_spot_options[0])), 1 },
	{ "SMOKE_M.SHP", 1, 1, NULL, 0, 1 },
	{ "RADAR.GDI", 1, 0, k_spot_radar, (int)(sizeof(k_spot_radar) / sizeof(k_spot_radar[0])), 1 },
	{ "TREX.SHP", 1, 0, k_spot_trex, (int)(sizeof(k_spot_trex) / sizeof(k_spot_trex[0])), 1 },
	{ "NUKE.SHP", 1, 0, k_spot_nuke, (int)(sizeof(k_spot_nuke) / sizeof(k_spot_nuke[0])), 1, 1 },
	{ "E4.SHP", 1, 0, k_spot_e4, (int)(sizeof(k_spot_e4) / sizeof(k_spot_e4[0])), 1 },
	{ "TRANS.ICN", 0, 0, NULL, 0, 0 },
};

static int read_file(const char *path, unsigned char **out_data, size_t *out_len)
{
	FILE *f;
	long sz;
	unsigned char *buf;

	*out_data = NULL;
	*out_len = 0;

	f = fopen(path, "rb");
	if (!f)
		return 0;

	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return 0;
	}
	sz = ftell(f);
	if (sz < 0) {
		fclose(f);
		return 0;
	}
	if (fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return 0;
	}

	buf = (unsigned char *)malloc((size_t)sz);
	if (!buf) {
		fclose(f);
		return 0;
	}
	if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
		free(buf);
		fclose(f);
		return 0;
	}
	fclose(f);
	*out_data = buf;
	*out_len = (size_t)sz;
	return 1;
}

static int test_decode_frames(
    const char *label, const unsigned char *data, size_t len, int decode_all, const unsigned short *spot,
    int n_spot)
{
	unsigned short frames;
	unsigned long buf_bytes;
	unsigned char *buf;
	unsigned short f;
	int failures = 0;

	frames = Get_Build_Frame_Count(data);
	buf_bytes = Get_Build_Frame_BufferBytes(data);
	if (frames == 0 || buf_bytes == 0) {
		fprintf(stderr, "FAIL %s: invalid frame count or buffer size\n", label);
		return 1;
	}

	buf = (unsigned char *)malloc(buf_bytes);
	if (!buf) {
		fprintf(stderr, "FAIL %s: malloc(%lu)\n", label, (unsigned long)buf_bytes);
		return 1;
	}

	if (decode_all) {
		for (f = 0; f < frames; ++f) {
			if (!Build_Frame(data, f, buf, len)) {
				fprintf(stderr, "FAIL %s: Build_Frame(%u/%u)\n", label, (unsigned)f,
				    (unsigned)frames);
				++failures;
			}
		}
	} else {
		int i;
		for (i = 0; i < n_spot; ++i) {
			f = spot[i];
			if (f >= frames) {
				fprintf(stderr, "FAIL %s: spot frame %u >= %u\n", label, (unsigned)f,
				    (unsigned)frames);
				++failures;
				continue;
			}
			if (!Build_Frame(data, f, buf, len)) {
				fprintf(stderr, "FAIL %s: Build_Frame spot %u\n", label, (unsigned)f);
				++failures;
			}
		}
	}

	free(buf);
	return failures;
}

static uint16_t read_be16(const unsigned char *p)
{
	return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t read_be32(const unsigned char *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int test_shpx_convert(const char *label, const unsigned char *data, size_t len, int verify_tail_clip)
{
	RemixShpxPool pool;
	unsigned char *out = NULL;
	size_t out_len = 0;
	int rc;
	int failures = 0;

	remix_shpx_pool_init(&pool, REMIX_SHPX_POOL_ID_DEFAULT);
	rc = remix_shpx_convert(data, len, &out, &out_len, &pool, NULL);
	if (rc != 1 || !out || out_len < SHPX_PREFIX_SIZE) {
		fprintf(stderr, "FAIL %s: remix_shpx_convert rc=%d\n", label, rc);
		remix_shpx_pool_free(&pool);
		return 1;
	}
	if (memcmp(out, "SHPX", 4) != 0) {
		fprintf(stderr, "FAIL %s: missing SHPX magic\n", label);
		++failures;
	}
	if (pool.size == 0) {
		fprintf(stderr, "FAIL %s: empty SHPX pool\n", label);
		++failures;
	}

	{
		uint16_t frames = read_be16(out + 4);
		uint32_t clip_off = read_be32(out + 26);
		if (verify_tail_clip && frames > 0 && clip_off + (size_t)frames * 8u <= out_len) {
			const unsigned char *tail = out + clip_off + ((size_t)frames - 1u) * 8u;
			uint16_t cw = read_be16(tail + 4);
			uint16_t ch = read_be16(tail + 6);
			if (cw == 0 || ch == 0) {
				fprintf(stderr, "FAIL %s: last frame clip is empty (%ux%u)\n", label,
				    (unsigned)cw, (unsigned)ch);
				++failures;
			}
		}
	}

	free(out);
	remix_shpx_pool_free(&pool);
	return failures;
}

int main(int argc, char **argv)
{
	const char *shapes_dir = "testdata/shapes";
	size_t i;
	int failures = 0;
	int ran = 0;

	if (argc > 1)
		shapes_dir = argv[1];

	for (i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); ++i) {
		const ShpxTestCase *tc = &k_cases[i];
		char path[512];
		unsigned char *data = NULL;
		size_t len = 0;
		int is_kf;

		snprintf(path, sizeof(path), "%s/%s", shapes_dir, tc->filename);
		if (!read_file(path, &data, &len)) {
			fprintf(stderr, "FAIL: could not read %s\n", path);
			++failures;
			continue;
		}
		++ran;

		is_kf = remix_is_keyframe_shp(data, len);
		if (is_kf != tc->expect_keyframe) {
			fprintf(stderr, "FAIL %s: remix_is_keyframe_shp=%d expected %d\n", tc->filename, is_kf,
			    tc->expect_keyframe);
			++failures;
			free(data);
			continue;
		}

		if (tc->expect_keyframe) {
			failures += test_decode_frames(tc->filename, data, len, tc->decode_all_frames,
			    tc->spot_frames, tc->n_spot);
			if (tc->expect_shpx)
				failures += test_shpx_convert(tc->filename, data, len, tc->verify_tail_clip);
		} else if (remix_is_keyframe_shp(data, len)) {
			fprintf(stderr, "FAIL %s: non-KeyFrame blob flagged as KeyFrame\n", tc->filename);
			++failures;
		} else if (remix_shpx_convert(data, len, NULL, NULL, NULL, NULL) != -1) {
			fprintf(stderr, "FAIL %s: remix_shpx_convert should return -1\n", tc->filename);
			++failures;
		}

		free(data);
	}

	if (ran == 0) {
		fprintf(stderr, "FAIL: no test shapes found under %s\n", shapes_dir);
		return 1;
	}
	if (failures != 0) {
		fprintf(stderr, "SHPX tests: %d failure(s)\n", failures);
		return 1;
	}

	printf("OK remix SHPX decode (%d shapes)\n", ran);
	return 0;
}
