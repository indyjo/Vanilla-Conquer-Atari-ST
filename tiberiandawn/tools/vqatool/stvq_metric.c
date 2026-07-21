/*
 * stvq_metric.c - Weighted Y-DCT tile features + L2 distance.
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
static unsigned dct_ncoeffs = STVQ_DEFAULT_DCT_COEFFS;
static float color_gamma = STVQ_DEFAULT_GAMMA;
static float color_y_scale = STVQ_DEFAULT_Y_SCALE;
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

static void rebuild_sqrt_w(void)
{
	unsigned i, n;
	ensure_dct_tables();
	ensure_zigzag();
	n = dct_ncoeffs;
	if (n > STVQ_METRIC_MAX_COEFFS)
		n = STVQ_METRIC_MAX_COEFFS;
	if (n < 1)
		n = 1;
	dct_ncoeffs = n;
	for (i = 0; i < 64; i++) {
		unsigned u = zz_u[i], v = zz_v[i];
		float w = 1.0f / (1.0f + dct_alpha * (float)(u * u + v * v));
		sqrt_w[i] = sqrtf(w);
	}
}

void stvq_metric_set_dct(float alpha, unsigned ncoeffs)
{
	if (alpha < 0.0f)
		alpha = 0.0f;
	dct_alpha = alpha;
	dct_ncoeffs = ncoeffs ? ncoeffs : 1;
	rebuild_sqrt_w();
}

float stvq_metric_dct_alpha(void)
{
	return dct_alpha;
}

unsigned stvq_metric_dct_coeffs(void)
{
	return dct_ncoeffs;
}

void stvq_metric_set_color(float gamma, float y_scale)
{
	if (gamma < 0.0f)
		gamma = STVQ_DEFAULT_GAMMA;
	if (y_scale < 0.0f)
		y_scale = STVQ_DEFAULT_Y_SCALE;
	color_gamma = gamma;
	color_y_scale = y_scale;
}

float stvq_metric_gamma(void)
{
	return color_gamma;
}

float stvq_metric_y_scale(void)
{
	return color_y_scale;
}

void stvq_metric_set_palette_vga6(const uint8_t pal768[768], const uint8_t subset[16])
{
	float colors[768];
	PaletteOptColorParams params;
	int i;

	rebuild_sqrt_w();
	palette_opt_color_params_default(&params);
	params.gamma = color_gamma;
	params.y_scale = color_y_scale;
	palette_build_opt_colors_params(pal768, colors, &params);
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

static void feat_from_yuv_planes(const float *yplane, float *out_feat)
{
	float coeff[64];
	unsigned i, n = dct_ncoeffs;

	dct8x8(yplane, coeff);
	for (i = 0; i < n; i++) {
		unsigned u = zz_u[i], v = zz_v[i];
		out_feat[i] = sqrt_w[i] * coeff[v * 8 + u];
	}
}

void stvq_metric_feat_from_indices(const uint8_t idx[64], float *out_feat)
{
	float yplane[64];
	int i;
	if (!metric_ready) {
		memset(out_feat, 0, dct_ncoeffs * sizeof(float));
		return;
	}
	for (i = 0; i < 64; i++)
		yplane[i] = yuv[idx[i]][0]; /* Y */
	feat_from_yuv_planes(yplane, out_feat);
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
	unsigned i, n = dct_ncoeffs;
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
	float fa[STVQ_METRIC_MAX_COEFFS], fb[STVQ_METRIC_MAX_COEFFS];
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
