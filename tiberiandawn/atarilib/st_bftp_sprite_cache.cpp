/*
 * LRU-cached planar + 1bpp mask sprite cache for Buffer_Frame_To_Page (Atari ST).
 */

#include "st_bftp_sprite_cache.h"

#include "c2p.h"
#include "st_blitter_blit.h"

#include "lrucache.h"

#include <stdint.h>

#include <cstdlib>
#include <cstring>
#include <new>

#ifndef ST_BFTP_CACHE_CAPACITY_32
#define ST_BFTP_CACHE_CAPACITY_32 24
#endif
#ifndef ST_BFTP_CACHE_CAPACITY_64
#define ST_BFTP_CACHE_CAPACITY_64 24
#endif
#ifndef ST_BFTP_CACHE_CAPACITY_96
#define ST_BFTP_CACHE_CAPACITY_96 6
#endif
enum { BFTP_D32 = 32, BFTP_D64 = 64, BFTP_D96 = 96 };

/*
 * Run lazy_frame_fill (Build_Frame) at most once per planar composite; only LRU miss triggers fill.
 */
struct BftpLazyGate {
	unsigned long (*fill)(void *ctx);
	void *ctx;
	unsigned char decoded; /* 1 after first successful fill */
};

/*
 * UnitShadow ghost rows (see DISPLAY.CPP Conquer_Build_Translucent_Table) fade LTGREEN→BLACK against
 * a logical backdrop palette index; without reading the framebuffer, use BLACK (EGA logical 12).
 */
enum { BFTP_GHOST_SYNTH_BACKDROP_IX = 12 };

struct BftpKey {
	uint32_t src_key; /* identity + full-frame w/h + stride (+ remap tokens folded in dispatch) */
	uint16_t bw; /* full_w */
	uint16_t bh; /* full_h */
	uint8_t tier_idx;
	uint8_t mode_pack; /* 0 plain, 1 fade, 2 ghost */
	bool trans_flag;
	uint32_t fade_token; /* 0 none; else stable mix of remap table identity (not raw pointers) */
	uint32_t ghost_token;

	bool operator==(const BftpKey &o) const
	{
		return src_key == o.src_key && bw == o.bw && bh == o.bh
		       && tier_idx == o.tier_idx
		       && mode_pack == o.mode_pack && trans_flag == o.trans_flag
		       && fade_token == o.fade_token && ghost_token == o.ghost_token;
	}
};

struct BftpKeyHash {
	size_t operator()(const BftpKey &k) const
	{
		size_t h = (size_t)k.src_key;
		h ^= ((size_t)k.fade_token << 17) ^ ((size_t)k.ghost_token << 3);
		h ^= ((size_t)k.bw << 16) ^ k.bh;
		h ^= (size_t)(((unsigned)k.tier_idx) | ((unsigned)k.mode_pack << 8));
		h ^= k.trans_flag ? (size_t)0xdeadbeefu : 0;
		return h;
	}
};

struct BftpTier {
	int dim = 0;
	int capacity = 0;
	uint8_t *planar_base = nullptr;
	uint8_t *mask_base = nullptr;
	int planar_bpl = 0;
	int planar_slot_sz = 0;
	int mask_bpl = 0;
	int mask_slot_sz = 0;
	LruCache<BftpKey, uint16_t, BftpKeyHash> *lru = nullptr;
};

static BftpTier g_bf_tiers[3];
static uint8_t *g_bf_slab = nullptr;
static bool g_bf_inited = false;

/*
 * 32-bit token for distinguishing objects by pointer identity without storing naked addresses in LRU keys.
 */
static uint32_t bftp_ptr_token(void const *p)
{
	if (p == nullptr) {
		return 0;
	}
	uintptr_t const u = (uintptr_t)(void const *)p;
	uint32_t h = 2166136261u;
	h ^= (uint32_t)u;
	h *= 16777619u;
#if defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ >= 8
	h ^= (uint32_t)(((unsigned long long)u) >> 32);
	h *= 16777619u;
#endif
	return h;
}

extern "C" long ST_BFTP_Frame_Identity_Key(void const *blobs_root, int frame_index)
{
	if (!blobs_root || frame_index < 0) {
		return 0L;
	}
	uint32_t h = bftp_ptr_token(blobs_root);
	h ^= ((uint32_t)(unsigned short)frame_index) * 709607003u ^ 419513369u;
	h *= 16777619u;
	return (long)(unsigned long)(unsigned)h;
}

/*
 * Full-frame geometry + opaque identity_key — no buffer pointers, no viewport clip/subrect.
 */
static uint32_t bftp_lru_identity_hash(unsigned full_w,
	unsigned full_h,
	unsigned row_stride_px,
	long identity_key)
{
	uint32_t h = 2166136261u;

	if (identity_key != (long)0) {
#if defined(__SIZEOF_LONG__) && (__SIZEOF_LONG__ >= 8)
		h ^= (uint32_t)(unsigned long)identity_key;
		h *= 16777619u;
		h ^= (uint32_t)(((unsigned long)identity_key) >> 32);
		h *= 16777619u;
#else
		h ^= (uint32_t)(unsigned long)identity_key;
		h *= 16777619u;
#endif
	}

	h ^= (uint32_t)full_w;
	h *= 16777619u;
	h ^= (uint32_t)full_h;
	h *= 16777619u;
	h ^= (uint32_t)row_stride_px;
	return h;
}

/*
 * Write one 16-color ST low-res planar pixel into a BFTP scratch atlas slot (same word/bit layout
 * as the screen buffer the blitter reads). Used on LRU fill when trans/fade/ghost prevent bulk C2P;
 * caller passes the final display nibble (typically C2P_Map8ToPlanar4 on sprite-local coords).
 * Clamps by returning if (x,y) is outside width_px × height_px.
 */
static inline void bftp_scratch_put_px(
	uint8_t *base, int row_bytes, int width_px, int height_px, int x, int y, unsigned char color4)
{
	if (!base || x < 0 || y < 0 || x >= width_px || y >= height_px || row_bytes <= 0)
		return;
	uint8_t *p = base + y * row_bytes + (x >> 4) * 8 + ((x >> 3) & 1);
	const int bitnum = 7 - (x & 7);
	const uint8_t maskbit = (uint8_t)(1u << (unsigned)bitnum);
	const uint8_t c = (uint8_t)(color4 & 15);
	for (int pl = 0; pl < 4; pl++) {
		uint8_t *pb = p + pl * 2;
		if (c & (uint8_t)(1u << pl))
			*pb |= maskbit;
		else
			*pb &= (uint8_t)~maskbit;
	}
}

/* Clear mask bit → ST blitter clears dest before OR-merge. Leave 1 to preserve backdrop. */
static inline void bftp_mask_mark_writes(uint8_t *maskbm, int rowb, int x, int y)
{
	uint8_t *b = maskbm + (size_t)y * (size_t)rowb + (size_t)(x >> 3);
	*b = (uint8_t)(*b & (uint8_t)~(0x80u >> (x & 7)));
}

static int bftp_pick_tier_dim(int need)
{
	if (need <= BFTP_D32)
		return BFTP_D32;
	if (need <= BFTP_D64)
		return BFTP_D64;
	if (need <= BFTP_D96)
		return BFTP_D96;
	return 0;
}

/*
 * Square LRU slot edge for BFTP: max of blit height and blit width rounded up to ST 16-pixel words.
 * Destination X skew is handled in the blitter (FXSR/NFSR); the atlas does not widen with screen x.
 */
static int bftp_tier_need_pixels(int blit_w, int blit_h)
{
	const int w_round = ((blit_w + 15) >> 4) << 4;
	return blit_h > w_round ? blit_h : w_round;
}

static BftpTier *bftp_tier_from_dim(int tier_dim)
{
	for (int t = 0; t < 3; ++t) {
		if (g_bf_tiers[t].dim == tier_dim && g_bf_tiers[t].lru)
			return &g_bf_tiers[t];
	}
	return nullptr;
}

static void bftp_maybe_init(void);

void ST_BFTP_Init_Sprite_Caches(void)
{
	bftp_maybe_init();
}

static void bftp_maybe_init(void)
{
	if (g_bf_inited)
		return;

	const int dims[3] = { BFTP_D32, BFTP_D64, BFTP_D96 };
	const int caps[3] = { ST_BFTP_CACHE_CAPACITY_32, ST_BFTP_CACHE_CAPACITY_64,
		              ST_BFTP_CACHE_CAPACITY_96 };

	size_t total = 0;
	int planar_bp[3], mask_bp[3], ps_sz[3], ms_sz[3];
	for (int t = 0; t < 3; ++t) {
		const int d = dims[t];
		planar_bp[t] = ((d + 15) >> 4) * 8;
		mask_bp[t] = ((d + 15) >> 4) * 2;
		ps_sz[t] = planar_bp[t] * d;
		ms_sz[t] = mask_bp[t] * d;
		total += (size_t)caps[t] * (size_t)ps_sz[t];
		total += (size_t)caps[t] * (size_t)ms_sz[t];
	}

	g_bf_slab = (uint8_t *)malloc(total);
	if (!g_bf_slab) {
		g_bf_inited = true;
		return;
	}

	uint8_t *walk = g_bf_slab;
	for (int t = 0; t < 3; ++t) {
		BftpTier &tr = g_bf_tiers[t];
		tr.dim = dims[t];
		tr.capacity = caps[t];
		tr.planar_bpl = planar_bp[t];
		tr.mask_bpl = mask_bp[t];
		tr.planar_slot_sz = ps_sz[t];
		tr.mask_slot_sz = ms_sz[t];
		tr.planar_base = walk;
		walk += (size_t)tr.capacity * (size_t)tr.planar_slot_sz;
		tr.mask_base = walk;
		walk += (size_t)tr.capacity * (size_t)tr.mask_slot_sz;

		tr.lru = nullptr;
		if (tr.capacity > 0) {
			tr.lru =
			    new (std::nothrow) LruCache<BftpKey, uint16_t, BftpKeyHash>((size_t)tr.capacity);
			if (!tr.lru)
				break;
			for (uint16_t i = 0; i < (uint16_t)tr.capacity; ++i) {
				BftpKey dk;
				std::memset(&dk, 0, sizeof(dk));
				dk.src_key = UINT32_MAX ^ ((((uint32_t)t) << 20) ^ (uint32_t)i);
				dk.bw = (uint16_t)tr.dim;
				dk.bh = (uint16_t)tr.dim;
				dk.tier_idx = (uint8_t)t;
				dk.mode_pack = 0xFF;
				dk.fade_token = (uint32_t)t + 1u;
				dk.ghost_token = 0x1000u + (uint32_t)i;
				dk.trans_flag = false;
				tr.lru->put(dk, i);
			}
		}
	}

	g_bf_inited = true;
}

static BOOL bftp_do_blitter(
	BOOL use_trans_merge,
	uint8_t *dst_root_fb,
	int dx_abs,
	int dy_abs,
	const uint8_t *planar,
	int planar_rowb,
	const uint8_t *maskbm,
	int mask_rowb,
	int tier_dim,
	int sx_abs,
	int sy_abs,
	int blit_w,
	int blit_h)
{
	if (use_trans_merge) {
		if (!ST_Blitter_Mask_And_Planar_Rect(
			maskbm,
			mask_rowb,
			tier_dim,
			tier_dim,
			sx_abs,
			sy_abs,
			dst_root_fb,
			ST_PLANAR_BYTES_PER_LINE,
			ST_PLANAR_WIDTH,
			ST_PLANAR_HEIGHT,
			dx_abs,
			dy_abs,
			blit_w,
			blit_h))
			return FALSE;
		if (!ST_Blitter_Planar_Rect_Blit_Or(planar,
				planar_rowb,
				tier_dim,
				tier_dim,
				sx_abs,
				sy_abs,
				dst_root_fb,
				ST_PLANAR_BYTES_PER_LINE,
				ST_PLANAR_WIDTH,
				ST_PLANAR_HEIGHT,
				dx_abs,
				dy_abs,
				blit_w,
			blit_h))
			return FALSE;
		return TRUE;
	}
	return ST_Blitter_Planar_Rect_Blit(planar,
		   planar_rowb,
		   tier_dim,
		   tier_dim,
		   sx_abs,
		   sy_abs,
		   dst_root_fb,
		   ST_PLANAR_BYTES_PER_LINE,
		   ST_PLANAR_WIDTH,
		   ST_PLANAR_HEIGHT,
		   dx_abs,
		   dy_abs,
		   blit_w,
		   blit_h)
		       ? TRUE
		       : FALSE;
}

static BOOL bftp_fill_slot_pixels(
	BftpTier *tr,
	uint16_t slot,
	uint8_t *dst_root_fb,
	const uint8_t *src,
	int bw,
	int bh,
	int stride,
	int ax0,
	int ay0,
	int trans,
	const uint8_t *ghost_tab,
	const uint8_t *fade_tab)
{
	const int dim = tr->dim;
	const int planar_rowb = tr->planar_bpl;
	const int mask_rowb = tr->mask_bpl;

	uint8_t *planar = tr->planar_base + (size_t)slot * (size_t)tr->planar_slot_sz;
	uint8_t *maskbm = tr->mask_base + (size_t)slot * (size_t)tr->mask_slot_sz;

	std::memset(planar, 0, (size_t)tr->planar_slot_sz);
	std::memset(maskbm, 0xFF, (size_t)tr->mask_slot_sz);

	/* Fully opaque chunky → scratch planar fast path */
	if (!ghost_tab && !fade_tab && !trans) {
		/* abs_x0/abs_y0 = 0: Bayer phase from sprite-local (col,row) only — cache is placement-agnostic. */
		C2P_Render_Logical_To_Planar_Rect(
			src,
			bw,
			bh,
			stride,
			planar,
			planar_rowb,
			dim,
			dim,
			0,
			0,
			0,
			0);
		return TRUE;
	}

	(void)dst_root_fb;
	(void)ax0;
	(void)ay0;

	const BOOL masked_merge = (ghost_tab != nullptr) || (trans != 0);
	const uint8_t *const ghost_cls = ghost_tab;
	const uint8_t *const ghost_blend = ghost_cls ? ghost_cls + 256 : nullptr;

	for (int row = 0; row < bh; ++row) {
		const uint8_t *srow = src + (size_t)row * (size_t)stride;
		for (int col = 0; col < bw; ++col) {
			const uint8_t raw = srow[col];
			if (trans && raw == 0)
				continue;

			unsigned char pal8 = raw;

			if (ghost_cls && ghost_blend) {
				const uint8_t cls = ghost_cls[raw];
				if (cls != 0xFFu) {
					/*
					 * Sprite-local checkerboard mask dither (~50%); must not use screen coords
					 * so cached textures are valid at any placement.
					 */
					if (((col ^ row) & 1) != 0)
						continue;
					pal8 = ghost_blend[(size_t)cls * 256u + BFTP_GHOST_SYNTH_BACKDROP_IX];
				} else {
					pal8 = fade_tab ? fade_tab[raw] : raw;
				}
			} else if (fade_tab) {
				pal8 = fade_tab[raw];
			}

			const unsigned char c4 = C2P_Map8ToPlanar4(col, row, pal8);
			bftp_scratch_put_px(planar, planar_rowb, dim, dim, col, row, c4);
			if (masked_merge)
				bftp_mask_mark_writes(maskbm, mask_rowb, col, row);
		}
	}
	return TRUE;
}

static long bftp_cached_tile_dispatch(uint8_t *dst_root_fb,
	int ax0,
	int ay0,
	int clip_w,
	int clip_h,
	int src_stride,
	int full_w,
	int full_h,
	int trans,
	const uint8_t *ghost_tab,
	const uint8_t *fade_tab,
	int tier_dim,
	const uint8_t *raster_base,
	int clip_ox,
	int clip_oy,
	long identity_key,
	BftpLazyGate *lazy_gate)
{
	BftpTier *tr = bftp_tier_from_dim(tier_dim);
	if (!tr || !tr->lru)
		return 0;

	uint8_t mode_pack = 0;
	if (ghost_tab)
		mode_pack = 2;
	else if (fade_tab)
		mode_pack = 1;
	else
		mode_pack = 0;

	BftpKey want;
	want.src_key = bftp_lru_identity_hash((unsigned)full_w, (unsigned)full_h,
	    (unsigned)src_stride, identity_key);
	want.bw = (uint16_t)full_w;
	want.bh = (uint16_t)full_h;
	want.tier_idx =
	    (uint8_t)((tier_dim == BFTP_D32) ? 0 : (tier_dim == BFTP_D64) ? 1 : 2);
	want.mode_pack = mode_pack;
	want.trans_flag = trans != 0;
	want.fade_token = bftp_ptr_token((void const *)fade_tab);
	want.ghost_token = bftp_ptr_token((void const *)ghost_tab);

	uint16_t slot = 0;
	if (!tr->lru->get(want, slot)) {
		if (!tr->lru->retarget_oldest_slot(want, slot))
			return 0;
		if (lazy_gate != nullptr && lazy_gate->fill != nullptr && lazy_gate->decoded == 0) {
			unsigned long const built = lazy_gate->fill(lazy_gate->ctx);
			if (built == 0UL) {
				return 0;
			}
			if ((const uint8_t *)(uintptr_t)built != raster_base) {
				return 0;
			}
			lazy_gate->decoded = 1;
		}
		if (!bftp_fill_slot_pixels(
			    tr,
			    slot,
			    dst_root_fb,
			    raster_base,
			    full_w,
			    full_h,
			    src_stride,
			    ax0,
			    ay0,
			    trans,
			    ghost_tab,
			    fade_tab))
			return 0;
	}

	const uint8_t *planar = tr->planar_base + (size_t)slot * (size_t)tr->planar_slot_sz;
	const uint8_t *maskbm = tr->mask_base + (size_t)slot * (size_t)tr->mask_slot_sz;

	const BOOL use_merge =
	    (trans != 0 || ghost_tab != nullptr) ? TRUE : FALSE;

	if (!bftp_do_blitter(use_merge ? TRUE : FALSE,
		    dst_root_fb,
		    ax0,
		    ay0,
		    planar,
		    tr->planar_bpl,
		    maskbm,
		    tr->mask_bpl,
		    tier_dim,
		    clip_ox,
		    clip_oy,
		    clip_w,
		    clip_h))
		return 0;

	return (long)((size_t)clip_w * (size_t)clip_h);
}

/*
 * Take one rectangle of chunky (8-bit indexed) pixels and try to show it on the ST low-res planar
 * framebuffer using the BFTP path: “find or build a small hardware-friendly scratch texture, then
 * let the blitter copy it to the screen.” This function is the front door for that work for a
 * single rectangle (no tiling of oversized draws here).
 *
 * What it actually does:
 *   1) Square LRU slot side from full decoded frame max(full_w, round16(full_h)), pick tier 32/64/96.
 *   2) Otherwise calls bftp_cached_tile_dispatch, which: looks up an LRU cache entry for the full
 *      decoded frame (identity_key, full_w/full_h, stride, fade/ghost); on miss fills from raster_base
 *      (optionally lazy_decode_miss once); then blits only the visible clip via blitter sx/sy.
 *
 * Return: blit_w*blit_h if the blitter path reports success, else 0 (skip, bad blit, etc.).
 *
 * Parameters:
 *   full_w/full_h — Decoded chunky frame extents at raster_base (stride src_stride ≥ full_w).
 *   Visible region: raster_ox, raster_oy, blit_w, blit_h — clip only affects the blitter, not LRU key.
 */
static long bftp_planar_composite_impl(uint8_t *dst_root_fb,
	int ax0,
	int ay0,
	int blit_w,
	int blit_h,
	int src_stride,
	int full_w,
	int full_h,
	int trans,
	const uint8_t *ghost_tab,
	const uint8_t *fade_tab,
	const uint8_t *raster_base,
	int raster_ox,
	int raster_oy,
	long identity_key,
	BftpLazyGate *lazy_gate)
{
	const int tier_need = bftp_tier_need_pixels(full_w, full_h);
	const int tier_dim = bftp_pick_tier_dim(tier_need);

	if (!tier_dim) {
		return 0;
	}

	const long acc = bftp_cached_tile_dispatch(dst_root_fb,
	    ax0,
	    ay0,
	    blit_w,
	    blit_h,
	    src_stride,
	    full_w,
	    full_h,
	    trans,
	    ghost_tab,
	    fade_tab,
	    tier_dim,
	    raster_base,
	    raster_ox,
	    raster_oy,
	    identity_key,
	    lazy_gate);
	return acc ? acc : 0;
}

long ST_BFTP_Buffer_Frame_Planar_Composite(uint8_t *dst_root_fb,
	int ax0,
	int ay0,
	const uint8_t *src,
	int blit_w,
	int blit_h,
	int src_stride,
	int trans,
	const uint8_t *ghost_tab,
	const uint8_t *fade_tab,
	const uint8_t *raster_base,
	int raster_ox,
	int raster_oy,
	int full_w,
	int full_h,
	long identity_key,
	unsigned long (*lazy_decode_miss)(void *user_ctx),
	void *lazy_decode_ctx)
{
	bftp_maybe_init();
	if (!dst_root_fb || blit_w <= 0 || blit_h <= 0 || src_stride <= 0 || !raster_base)
		return 0;
	if (full_w <= 0 || full_h <= 0 || full_w > src_stride)
		return 0;
	if (raster_ox < 0 || raster_oy < 0)
		return 0;
	if (raster_ox + blit_w > full_w || raster_oy + blit_h > full_h)
		return 0;
	if (!g_bf_slab)
		return 0;
	if (src != raster_base + (size_t)raster_oy * (size_t)src_stride + (size_t)raster_ox)
		return 0;

	BftpLazyGate gate_stack;
	BftpLazyGate *gate_ptr = NULL;
	if (lazy_decode_miss != nullptr) {
		gate_stack.fill = lazy_decode_miss;
		gate_stack.ctx = lazy_decode_ctx;
		gate_stack.decoded = 0;
		gate_ptr = &gate_stack;
	}

	return bftp_planar_composite_impl(dst_root_fb,
	    ax0,
	    ay0,
	    blit_w,
	    blit_h,
	    src_stride,
	    full_w,
	    full_h,
	    trans,
	    ghost_tab,
	    fade_tab,
	    raster_base,
	    raster_ox,
	    raster_oy,
	    identity_key,
	    gate_ptr);
}
