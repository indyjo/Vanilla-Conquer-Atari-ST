/*
 * stvq_metric.c - Weighted YUV-DCT tile features + L2 distance.
 */
#include "stvq_metric.h"

#include "palette_color.h"
#include "stvq_c2p.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Zig-zag (u,v) filled once. */
static uint8_t zz_u[64];
static uint8_t zz_v[64];
static int zigzag_ready;

static void ensure_zigzag(void)
{
	int u = 0, v = 0, i;
	if (zigzag_ready)
		return;
	for (i = 0; i < 64; i++) {
		zz_u[i] = (uint8_t)u;
		zz_v[i] = (uint8_t)v;
		if ((u + v) & 1) {
			if (u == 7)
				v++;
			else if (v == 0)
				u++;
			else {
				u++;
				v--;
			}
		} else {
			if (v == 7)
				u++;
			else if (u == 0)
				v++;
			else {
				u--;
				v++;
			}
		}
	}
	zigzag_ready = 1;
}

static float yuv[256][3];
static uint8_t pen_to_vga[16];
static float dct_alpha = STVQ_DEFAULT_DCT_ALPHA;
static unsigned dct_y_coeffs = STVQ_DEFAULT_DCT_COEFFS;
static unsigned dct_chroma_coeffs = STVQ_DEFAULT_DCT_CHROMA_COEFFS;
static float color_gamma = STVQ_DEFAULT_GAMMA;
static float sqrt_w[64]; /* zig-zag slot → √w(u,v) */
static float dct_cos[8][8]; /* cos(π/8*(n+½)*k) */
static float dct_s0, dct_sk; /* 1/√8 , √(2/8) */
static int metric_ready;
static int dct_tables_ready;

static void ensure_dct_tables(void)
{
	int n, k;
	if (dct_tables_ready)
		return;
	dct_s0 = 1.0f / sqrtf(8.0f);
	dct_sk = sqrtf(2.0f / 8.0f);
	for (k = 0; k < 8; k++) {
		for (n = 0; n < 8; n++)
			dct_cos[k][n] = cosf((float)(M_PI / 8.0 * (n + 0.5) * k));
	}
	dct_tables_ready = 1;
}

/* Clamp each plane to a valid zig-zag count (Y ≥ 1). feat_len = y + 2*c. */
static void clamp_plane_counts(void)
{
	unsigned y = dct_y_coeffs;
	unsigned c = dct_chroma_coeffs;

	if (y < 1)
		y = 1;
	if (y > STVQ_METRIC_MAX_COEFFS)
		y = STVQ_METRIC_MAX_COEFFS;
	if (c > STVQ_METRIC_MAX_COEFFS)
		c = STVQ_METRIC_MAX_COEFFS;
	dct_y_coeffs = y;
	dct_chroma_coeffs = c;
}

static void rebuild_sqrt_w(void)
{
	unsigned i;
	ensure_dct_tables();
	ensure_zigzag();
	clamp_plane_counts();
	for (i = 0; i < 64; i++) {
		unsigned u = zz_u[i], v = zz_v[i];
		float w = 1.0f / (1.0f + dct_alpha * (float)(u * u + v * v));
		sqrt_w[i] = sqrtf(w);
	}
}

void stvq_metric_set_dct(float alpha, unsigned y_coeffs, unsigned chroma_coeffs)
{
	if (alpha < 0.0f)
		alpha = 0.0f;
	dct_alpha = alpha;
	dct_y_coeffs = y_coeffs ? y_coeffs : 1;
	dct_chroma_coeffs = chroma_coeffs; /* 0 = Y-only */
	rebuild_sqrt_w();
}

float stvq_metric_dct_alpha(void)
{
	return dct_alpha;
}

unsigned stvq_metric_dct_coeffs(void)
{
	return dct_y_coeffs;
}

unsigned stvq_metric_dct_chroma_coeffs(void)
{
	return dct_chroma_coeffs;
}

unsigned stvq_metric_feat_len(void)
{
	return dct_y_coeffs + 2u * dct_chroma_coeffs;
}

void stvq_metric_set_gamma(float gamma)
{
	if (gamma < 0.0f)
		gamma = STVQ_DEFAULT_GAMMA;
	color_gamma = gamma;
}

float stvq_metric_gamma(void)
{
	return color_gamma;
}

void stvq_metric_set_palette_vga6(const uint8_t pal768[768], const uint8_t subset[16])
{
	float colors[768];
	uint8_t san[768];
	PaletteOptColorParams params;
	int i;

	rebuild_sqrt_w();
	for (i = 0; i < 768; i++)
		san[i] = (uint8_t)(pal768[i] & 63u);
	palette_opt_color_params_default(&params);
	params.gamma = color_gamma;
	params.y_scale = 1.0f; /* luma weight via DCT coeff counts, not Y scale */
	palette_build_opt_colors_params(san, colors, &params);
	memcpy(yuv, colors, sizeof(yuv));
	for (i = 0; i < 16; i++)
		pen_to_vga[i] = subset[i];
	metric_ready = 1;
}

/* Orthonormal 2D DCT-II of 8×8 block → out[u][v] row-major 64. */
static void dct8x8(const float *in, float *out)
{
	float tmp[8][8];
	int x, y, u, v;

	ensure_dct_tables();
	/* rows */
	for (y = 0; y < 8; y++) {
		for (u = 0; u < 8; u++) {
			float s = 0.0f;
			const float *row = in + y * 8;
			for (x = 0; x < 8; x++)
				s += row[x] * dct_cos[u][x];
			tmp[y][u] = s * (u == 0 ? dct_s0 : dct_sk);
		}
	}
	/* cols */
	for (u = 0; u < 8; u++) {
		for (v = 0; v < 8; v++) {
			float s = 0.0f;
			for (y = 0; y < 8; y++)
				s += tmp[y][u] * dct_cos[v][y];
			out[v * 8 + u] = s * (v == 0 ? dct_s0 : dct_sk);
		}
	}
}

/* Pack first `n` zig-zag weighted DCT coeffs of `plane` into out[0..n). */
static void pack_plane_feats(const float *plane, unsigned n, float *out)
{
	float coeff[64];
	unsigned i;

	if (!n)
		return;
	dct8x8(plane, coeff);
	for (i = 0; i < n; i++) {
		unsigned u = zz_u[i], v = zz_v[i];
		out[i] = sqrt_w[i] * coeff[v * 8 + u];
	}
}

void stvq_metric_feat_from_indices(const uint8_t idx[64], float *out_feat)
{
	float plane[64];
	unsigned i, ny = dct_y_coeffs, nc = dct_chroma_coeffs;

	if (!metric_ready) {
		memset(out_feat, 0, stvq_metric_feat_len() * sizeof(float));
		return;
	}
	for (i = 0; i < 64; i++)
		plane[i] = yuv[idx[i]][0];
	pack_plane_feats(plane, ny, out_feat);
	if (!nc)
		return;
	for (i = 0; i < 64; i++)
		plane[i] = yuv[idx[i]][1];
	pack_plane_feats(plane, nc, out_feat + ny);
	for (i = 0; i < 64; i++)
		plane[i] = yuv[idx[i]][2];
	pack_plane_feats(plane, nc, out_feat + ny + nc);
}

void stvq_metric_feat_from_pens(const uint8_t pens[64], float *out_feat)
{
	uint8_t idx[64];
	int i;
	for (i = 0; i < 64; i++)
		idx[i] = pen_to_vga[pens[i] & 15];
	stvq_metric_feat_from_indices(idx, out_feat);
}

void stvq_metric_feat_from_tile32(const uint8_t tile32[32], float *out_feat)
{
	uint8_t pens[64];
	stvq_unpack_tile_32(tile32, pens);
	stvq_metric_feat_from_pens(pens, out_feat);
}

unsigned stvq_metric_feat_dist(const float *a, const float *b)
{
	return stvq_metric_feat_dist_lim(a, b, ~0u);
}

unsigned stvq_metric_feat_dist_lim(const float *a, const float *b, unsigned max_d)
{
	unsigned i, n = stvq_metric_feat_len();
	float sum = 0.0f;

	for (i = 0; i < n; i++) {
		float d = a[i] - b[i];
		sum += d * d;
		if ((unsigned)(sum * STVQ_METRIC_SCALE) >= max_d)
			return (unsigned)(sum * STVQ_METRIC_SCALE + 0.5f);
	}
	return (unsigned)(sum * STVQ_METRIC_SCALE + 0.5f);
}

unsigned stvq_src_pens_error(const uint8_t src_vga[64], const uint8_t pens[64])
{
	return stvq_src_pens_error_lim(src_vga, pens, ~0u);
}

unsigned stvq_src_pens_error_lim(const uint8_t src_vga[64], const uint8_t pens[64], unsigned max_d)
{
	float fa[STVQ_METRIC_MAX_FEAT_LEN], fb[STVQ_METRIC_MAX_FEAT_LEN];
	if (!metric_ready)
		return 0;
	stvq_metric_feat_from_indices(src_vga, fa);
	stvq_metric_feat_from_pens(pens, fb);
	return stvq_metric_feat_dist_lim(fa, fb, max_d);
}

unsigned stvq_src_tile_error(const uint8_t src_vga[64], const uint8_t tile32[32])
{
	uint8_t pens[64];
	if (!metric_ready)
		return 0;
	stvq_unpack_tile_32(tile32, pens);
	return stvq_src_pens_error(src_vga, pens);
}
