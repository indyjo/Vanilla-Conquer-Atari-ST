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
static float dct_chroma_weight = STVQ_DEFAULT_DCT_CHROMA_WEIGHT;
/* sqrt(STVQ_DEFAULT_DCT_CHROMA_WEIGHT); kept in sync by set_chroma_weight. */
static float dct_chroma_coeff_scale = 0.5f;
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

/* Clamp each plane to a valid zig-zag count (Y ≥ 1). */
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

static void set_chroma_weight(float chroma_weight)
{
	if (chroma_weight < 0.0f)
		chroma_weight = STVQ_DEFAULT_DCT_CHROMA_WEIGHT;
	dct_chroma_weight = chroma_weight;
	dct_chroma_coeff_scale = sqrtf(chroma_weight);
}

void stvq_metric_set_dct(float alpha, unsigned y_coeffs, unsigned chroma_coeffs, float chroma_weight)
{
	if (alpha < 0.0f)
		alpha = 0.0f;
	dct_alpha = alpha;
	dct_y_coeffs = y_coeffs ? y_coeffs : 1;
	dct_chroma_coeffs = chroma_coeffs; /* 0 = no kept chroma coeffs; energy still stored */
	set_chroma_weight(chroma_weight);
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

float stvq_metric_dct_chroma_weight(void)
{
	return dct_chroma_weight;
}

/* One energy slot when the plane is truncated; a full plane contributes only its coeffs. */
static unsigned plane_slots(unsigned n)
{
	return n + (n < STVQ_METRIC_MAX_COEFFS ? 1u : 0u);
}

static unsigned coeff_len(void)
{
	return dct_y_coeffs + 2u * dct_chroma_coeffs;
}

unsigned stvq_metric_feat_len(void)
{
	return plane_slots(dct_y_coeffs) + 2u * plane_slots(dct_chroma_coeffs);
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
	params.y_scale = 1.0f; /* luma vs chroma via dct_chroma_weight, not Y scale */
	/* Target-style build: full VGA6 (bits_per_channel ignored). */
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

/* Orthonormal DCT of one YUV plane (comp 0/1/2) into coeff[u + 8*v]. */
static void dct_plane(const uint8_t idx[64], int comp, float *coeff)
{
	float plane[64];
	unsigned i;

	for (i = 0; i < 64; i++)
		plane[i] = yuv[idx[i]][comp];
	dct8x8(plane, coeff);
}

/* First n zig-zag coeffs, scaled, into out[0..n). */
static void store_kept(const float *coeff, unsigned n, float scale, float *out)
{
	unsigned i;

	for (i = 0; i < n; i++) {
		unsigned u = zz_u[i], v = zz_v[i];
		out[i] = scale * sqrt_w[i] * coeff[v * 8 + u];
	}
}

/* Weighted energy of coeffs n..63, times energy_scale. 0 when the plane is full. */
static float tail_energy(const float *coeff, unsigned n, float energy_scale)
{
	unsigned i;
	float tail = 0.0f;

	if (n >= STVQ_METRIC_MAX_COEFFS || energy_scale == 0.0f)
		return 0.0f;
	for (i = n; i < STVQ_METRIC_MAX_COEFFS; i++) {
		unsigned u = zz_u[i], v = zz_v[i];
		float f = sqrt_w[i] * coeff[v * 8 + u];
		tail += f * f;
	}
	return energy_scale * tail;
}

void stvq_metric_feat_from_indices(const uint8_t idx[64], float *out_feat)
{
	float coeff[64];
	unsigned ny = dct_y_coeffs, nc = dct_chroma_coeffs;
	float ey, eu, ev;
	float *p;

	if (!metric_ready) {
		memset(out_feat, 0, stvq_metric_feat_len() * sizeof(float));
		return;
	}
	dct_plane(idx, 0, coeff);
	store_kept(coeff, ny, 1.0f, out_feat);
	ey = tail_energy(coeff, ny, 1.0f);
	p = out_feat + ny;
	if (dct_chroma_weight == 0.0f) {
		/* Luma-only: same layout, U/V coeffs and tails are zero. Skip both DCTs. */
		memset(p, 0, (stvq_metric_feat_len() - ny) * sizeof(float));
		if (ny < STVQ_METRIC_MAX_COEFFS)
			out_feat[coeff_len()] = ey;
		return;
	}
	dct_plane(idx, 1, coeff);
	store_kept(coeff, nc, dct_chroma_coeff_scale, p);
	eu = tail_energy(coeff, nc, dct_chroma_weight);
	p += nc;
	dct_plane(idx, 2, coeff);
	store_kept(coeff, nc, dct_chroma_coeff_scale, p);
	ev = tail_energy(coeff, nc, dct_chroma_weight);
	p += nc;
	if (ny < STVQ_METRIC_MAX_COEFFS)
		*p++ = ey;
	if (nc < STVQ_METRIC_MAX_COEFFS) {
		*p++ = eu;
		*p++ = ev;
	}
}

uint32_t stvq_metric_color_hash(const uint8_t idx[64])
{
	uint32_t h = 2166136261u;
	int i;

	for (i = 0; i < 64; i++) {
		h ^= idx[i];
		h *= 16777619u;
	}
	return h;
}

uint32_t stvq_metric_color_hash_pens(const uint8_t pens[64])
{
	uint8_t idx[64];
	int i;

	for (i = 0; i < 64; i++)
		idx[i] = pen_to_vga[pens[i] & 15];
	return stvq_metric_color_hash(idx);
}

uint32_t stvq_metric_color_hash_tile32(const uint8_t tile32[32])
{
	uint8_t pens[64];

	stvq_unpack_tile_32(tile32, pens);
	return stvq_metric_color_hash_pens(pens);
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

unsigned stvq_metric_feat_dist_hash_lim(const float *a, uint32_t hash_a, const float *b, uint32_t hash_b,
    unsigned max_d)
{
	if (hash_a == hash_b)
		return 0;
	return stvq_metric_feat_dist_lim(a, b, max_d);
}

unsigned stvq_metric_feat_dist_hash(const float *a, uint32_t hash_a, const float *b, uint32_t hash_b)
{
	return stvq_metric_feat_dist_hash_lim(a, hash_a, b, hash_b, ~0u);
}

unsigned stvq_metric_feat_dist(const float *a, const float *b)
{
	return stvq_metric_feat_dist_lim(a, b, ~0u);
}

unsigned stvq_metric_feat_dist_lim(const float *a, const float *b, unsigned max_d)
{
	unsigned i, ncoeff = coeff_len(), n = stvq_metric_feat_len();
	float sum = 0.0f;

	for (i = 0; i < ncoeff; i++) {
		float d = a[i] - b[i];
		sum += d * d;
		if ((unsigned)(sum * STVQ_METRIC_SCALE) >= max_d)
			return (unsigned)(sum * STVQ_METRIC_SCALE + 0.5f);
	}
	/* Tail slots are energies. Uncorrelated discarded coeffs → E_a + E_b. */
	for (; i < n; i++) {
		sum += a[i] + b[i];
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
	return stvq_metric_feat_dist_hash_lim(fa, stvq_metric_color_hash(src_vga), fb,
	    stvq_metric_color_hash_pens(pens), max_d);
}

unsigned stvq_src_tile_error(const uint8_t src_vga[64], const uint8_t tile32[32])
{
	uint8_t pens[64];
	if (!metric_ready)
		return 0;
	stvq_unpack_tile_32(tile32, pens);
	return stvq_src_pens_error(src_vga, pens);
}

unsigned stvq_tile_error(const uint8_t a[32], const uint8_t b[32])
{
	float fa[STVQ_METRIC_MAX_FEAT_LEN], fb[STVQ_METRIC_MAX_FEAT_LEN];
	if (!metric_ready)
		return 0;
	if (stvq_metric_color_hash_tile32(a) == stvq_metric_color_hash_tile32(b))
		return 0;
	stvq_metric_feat_from_tile32(a, fa);
	stvq_metric_feat_from_tile32(b, fb);
	return stvq_metric_feat_dist(fa, fb);
}
