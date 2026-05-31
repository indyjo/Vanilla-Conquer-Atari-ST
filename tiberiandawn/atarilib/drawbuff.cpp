/*
 * drawbuff.cpp - Drawing buffer functions for Atari ST/MiNT
 * 
 * This provides portable C implementations of buffer pixel operations
 */

#include "drawbuff.h"
#include "gbuffer.h"
#include "font.h"
#include "c2p.h"
#include "function.h"
#include "st_blitter_blit.h"
#include "st_sprite_cache.h"
#include "st_planar_draw.h"
#include "memflag.h"
#include <string.h>  // For memset
#include <stdint.h>
#include <stdio.h>
#include <mint/osbind.h>

#include "st_screen.h"

/* Kept local to avoid including CONQUER.CPP private define. */
static const int ST_SHAPE_TRANS_FLAG = 0x40;
int IKBD_Key_Is_Down(int vk);
static const int ST_TILE_LINEAR_W = 24;
static const int ST_TILE_LINEAR_H = 24;
static const int ST_TILE_LINEAR_BYTES = ST_TILE_LINEAR_W * ST_TILE_LINEAR_H;
/*
 * Keep planar scratch wider than tile width so we can place the tile at sx = dx mod 16.
 * This makes src/dst nibble alignment equal (skew_low = 0), avoiding problematic skew cases.
 */
static const int ST_TILE_PLANAR_CACHE_SLOTS = 128;
static const int ST_TILE_PLANAR_CACHE_TILES_PER_ROW = 13; /* 13*24=312, keep 8px right margin */
static const int ST_TILE_PLANAR_CACHE_W = 320;
static const int ST_TILE_PLANAR_CACHE_ROWS =
	(ST_TILE_PLANAR_CACHE_SLOTS + ST_TILE_PLANAR_CACHE_TILES_PER_ROW - 1) / ST_TILE_PLANAR_CACHE_TILES_PER_ROW;
static const int ST_TILE_PLANAR_CACHE_H = ST_TILE_PLANAR_CACHE_ROWS * ST_TILE_LINEAR_H;
static const int ST_TILE_PLANAR_CACHE_BPL = (ST_TILE_PLANAR_CACHE_W / 16) * 8;
static const int ST_TILE_PLANAR_CACHE_BYTES = ST_TILE_PLANAR_CACHE_BPL * ST_TILE_PLANAR_CACHE_H;
static const int ST_TILE_PLANAR_CACHE_ALIGN = 256;

static uint8_t *g_tile_linear_24x24 = NULL;
static uint8_t *g_tile_planar_cache_raw = NULL;
static uint8_t *g_tile_planar_cache_aligned = NULL;
static BOOL g_tile_scratch_init_attempted = FALSE;
static unsigned long g_tile_planar_cache_clock = 1;
static BOOL g_tile_cache_debug_show = FALSE;
static BOOL g_tile_cache_debug_key_prev = FALSE;

typedef struct STTilePlanarCacheEntry_ {
	unsigned long identity_key;
	unsigned long last_used_tick;
	unsigned short atlas_x;
	unsigned short atlas_y;
	BOOL valid;
} STTilePlanarCacheEntry;

static STTilePlanarCacheEntry g_tile_planar_cache[ST_TILE_PLANAR_CACHE_SLOTS];

enum { ST_HZ200_ADDR = 0x4BA };

static inline unsigned long ST_Read_Hz200(void)
{
	return *(volatile unsigned long *)ST_HZ200_ADDR;
}

static void ST_Tile_Cache_Debug_Toggle_Maybe(void)
{
	/* F10 alone: avoid clash with Ctrl+F10 (TOS console toggle in st_screen.cpp). */
	int ctrl = IKBD_Key_Is_Down(VK_CONTROL);
	BOOL down = (IKBD_Key_Is_Down(VK_F10) && !ctrl) ? TRUE : FALSE;
	if (down && !g_tile_cache_debug_key_prev) {
		g_tile_cache_debug_show = (g_tile_cache_debug_show == FALSE) ? TRUE : FALSE;
		if (g_tile_cache_debug_show && g_tile_planar_cache_aligned) {
			ST_Screen_Hardware_Set_Phys_Base(g_tile_planar_cache_aligned);
			printf("TileCache debug view: ON (phys=tile atlas)\n");
		} else if (VisiblePage.Get_Buffer()) {
			ST_Screen_Hardware_Set_Phys_Base(VisiblePage.Get_Buffer());
		}
	}
	g_tile_cache_debug_key_prev = down;
}

static uint8_t *Alloc_Planar_Blitter_Scratch(size_t bytes)
{
	long p = Mxalloc((long)bytes, MX_STRAM);
	if (p > 0L)
		return (uint8_t *)p;
	return NULL;
}

static inline unsigned long Next_Tile_Planar_Cache_Tick(void)
{
	unsigned long t = ++g_tile_planar_cache_clock;
	if (t == 0UL) {
		int i;
		g_tile_planar_cache_clock = 1UL;
		for (i = 0; i < ST_TILE_PLANAR_CACHE_SLOTS; ++i) {
			if (g_tile_planar_cache[i].valid) {
				g_tile_planar_cache[i].last_used_tick = 1UL;
			}
		}
		return 1UL;
	}
	return t;
}

static STTilePlanarCacheEntry *Find_Tile_Planar_Cache_Entry(
	unsigned long identity_key)
{
	int i;
	for (i = 0; i < ST_TILE_PLANAR_CACHE_SLOTS; ++i) {
		STTilePlanarCacheEntry *e = &g_tile_planar_cache[i];
		if (!e->valid)
			continue;
		if (e->identity_key == identity_key) {
			e->last_used_tick = Next_Tile_Planar_Cache_Tick();
			return e;
		}
	}
	return NULL;
}

static STTilePlanarCacheEntry *Reserve_Tile_Planar_Cache_Entry(void)
{
	int i;
	STTilePlanarCacheEntry *best = &g_tile_planar_cache[0];
	for (i = 0; i < ST_TILE_PLANAR_CACHE_SLOTS; ++i) {
		STTilePlanarCacheEntry *e = &g_tile_planar_cache[i];
		if (!e->valid)
			return e;
		if (e->last_used_tick < best->last_used_tick)
			best = e;
	}
	return best;
}

static inline BOOL GB_Uses_ST_Planar_Surface(GraphicBufferClass *gb)
{
	return gb && gb->Is_ST_Planar();
}

static inline int GB_ST_Planar_Row_Bytes(GraphicBufferClass *gb)
{
	if (!gb || !gb->Is_ST_Planar())
		return 0;
	int const p = gb->Get_Pitch();
	return (p > 0) ? p : ST_Planar_Row_Bytes(gb->Get_Width());
}

static BOOL Try_Blit_Cached_Terrain_Tile(
	GraphicViewPortClass *vp,
	unsigned long identity_key,
	int x_pixel,
	int y_pixel)
{
	const int vpw = vp->Get_Width();
	const int vph = vp->Get_Height();
	int dst_x = x_pixel;
	int dst_y = y_pixel;
	int clip_src_x = 0;
	int clip_src_y = 0;
	int clip_blit_w = ST_TILE_LINEAR_W;
	int clip_blit_h = ST_TILE_LINEAR_H;
	GraphicBufferClass *dst_gb;
	uint8_t *dst_root;
	int dx_abs;
	int dy_abs;
	STTilePlanarCacheEntry *cache_entry;

	if (dst_x < 0) {
		clip_src_x = -dst_x;
		clip_blit_w -= clip_src_x;
		dst_x = 0;
	}
	if (dst_y < 0) {
		clip_src_y = -dst_y;
		clip_blit_h -= clip_src_y;
		dst_y = 0;
	}
	if (dst_x + clip_blit_w > vpw) {
		clip_blit_w = vpw - dst_x;
	}
	if (dst_y + clip_blit_h > vph) {
		clip_blit_h = vph - dst_y;
	}
	if (clip_blit_w <= 0 || clip_blit_h <= 0) {
		return TRUE;
	}

	dst_gb = vp->Get_Graphic_Buffer();
	dst_root = (dst_gb && GB_Uses_ST_Planar_Surface(dst_gb))
		? (uint8_t *)dst_gb->Get_Buffer() : NULL;
	dx_abs = vp->Get_XPos() + dst_x;
	dy_abs = vp->Get_YPos() + dst_y;
	if (!dst_gb || !dst_root
		|| dx_abs < 0 || dy_abs < 0
		|| dx_abs + clip_blit_w > dst_gb->Get_Width()
		|| dy_abs + clip_blit_h > dst_gb->Get_Height()) {
		return FALSE;
	}

	cache_entry = Find_Tile_Planar_Cache_Entry(identity_key);
	if (!cache_entry) {
		return FALSE;
	}

	{
		int const dst_bpl = GB_ST_Planar_Row_Bytes(dst_gb);
		BOOL blit_ok = ST_Blitter_Planar_Rect_Blit(
		g_tile_planar_cache_aligned,
		ST_TILE_PLANAR_CACHE_BPL,
		ST_TILE_PLANAR_CACHE_W,
		ST_TILE_PLANAR_CACHE_H,
		(int)cache_entry->atlas_x + clip_src_x,
		(int)cache_entry->atlas_y + clip_src_y,
		dst_root,
		dst_bpl,
		dst_gb->Get_Width(),
		dst_gb->Get_Height(),
		dx_abs,
		dy_abs,
		clip_blit_w,
		clip_blit_h);
		return blit_ok;
	}
}

static BOOL Ensure_Terrain_Tile_Scratch(void)
{
	if (g_tile_linear_24x24 && g_tile_planar_cache_aligned)
		return TRUE;
	if (g_tile_scratch_init_attempted)
		return FALSE;

	g_tile_scratch_init_attempted = TRUE;
	g_tile_linear_24x24 = (uint8_t *)Alloc((unsigned long)ST_TILE_LINEAR_BYTES, MEM_NORMAL);
	g_tile_planar_cache_raw =
		Alloc_Planar_Blitter_Scratch((size_t)(ST_TILE_PLANAR_CACHE_BYTES + ST_TILE_PLANAR_CACHE_ALIGN - 1));
	if (g_tile_linear_24x24 && g_tile_planar_cache_raw) {
		int i;
		unsigned long long p = (unsigned long long)(const void *)g_tile_planar_cache_raw;
		unsigned long long aligned = (p + (unsigned long long)(ST_TILE_PLANAR_CACHE_ALIGN - 1))
			& ~((unsigned long long)(ST_TILE_PLANAR_CACHE_ALIGN - 1));
		g_tile_planar_cache_aligned = (uint8_t *)(void *)aligned;
		memset(g_tile_planar_cache_aligned, 0, (size_t)ST_TILE_PLANAR_CACHE_BYTES);
		memset(g_tile_planar_cache, 0, sizeof(g_tile_planar_cache));
		for (i = 0; i < ST_TILE_PLANAR_CACHE_SLOTS; ++i) {
			g_tile_planar_cache[i].atlas_x =
				(unsigned short)((i % ST_TILE_PLANAR_CACHE_TILES_PER_ROW) * ST_TILE_LINEAR_W);
			g_tile_planar_cache[i].atlas_y =
				(unsigned short)((i / ST_TILE_PLANAR_CACHE_TILES_PER_ROW) * ST_TILE_LINEAR_H);
		}
		return TRUE;
	}
	return FALSE;
}

static inline unsigned short Read_LE16_Unsafe(const unsigned char *p)
{
	return (unsigned short)((unsigned short)p[0] | ((unsigned short)p[1] << 8));
}

static inline unsigned long Read_LE32_Unsafe(const unsigned char *p)
{
	return (unsigned long)p[0]
		| ((unsigned long)p[1] << 8)
		| ((unsigned long)p[2] << 16)
		| ((unsigned long)p[3] << 24);
}

static inline BOOL VP_Is_Planar(GraphicViewPortClass *vp)
{
	if (!vp)
		return FALSE;
	GraphicBufferClass *gb = vp->Get_Graphic_Buffer();
	return gb != NULL && GB_Uses_ST_Planar_Surface(gb);
}

/* True when vp is the embedded GraphicViewPort part of a GraphicBufferClass (same object). */
static inline BOOL VP_Is_Root_Graphic_Buffer(GraphicViewPortClass *vp)
{
	if (!vp)
		return FALSE;
	GraphicBufferClass *gb = vp->Get_Graphic_Buffer();
	return gb != NULL && (void *)vp == (void *)gb;
}

/*
 * Row stride in bytes for indexing src_base[y*stride + x]:
 * - Planar ST: physical bytes per scanline for the full buffer (same for root and sub-viewports;
 *   coordinates use XPos/YPos + local x,y; never add XAdd — that is only for linear sub-windows).
 * - Root GraphicBufferClass (linear): Init stores Pitch as padding after Width (stride = Width+Pitch+XAdd).
 * - Attached linear viewports: Pitch+XAdd holds the backing buffer's bytes-per-row (see Attach in gbuffer.cpp).
 */
static inline int Get_Row_Stride(GraphicViewPortClass *vp) {
	if (!vp)
		return 0;
	GraphicBufferClass *gb = vp->Get_Graphic_Buffer();
	if (gb && GB_Uses_ST_Planar_Surface(gb)) {
		return GB_ST_Planar_Row_Bytes(gb);
	}
	if (VP_Is_Root_Graphic_Buffer(vp))
		return vp->Get_Width() + vp->Get_Pitch() + vp->Get_XAdd();
	int s = vp->Get_Pitch() + vp->Get_XAdd();
	return (s != 0) ? s : vp->Get_Width();
}

// Color translation table for font rendering
// This maps font palette indices (0-15) to actual color values
// Made non-static so it can be accessed from font.cpp
unsigned char ColorXlat[256] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
	
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	
	0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	
	0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	
	0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	
	0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	
	0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	
	0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	
	0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	
	0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	
	0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	
	0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	
	0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	
	0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	
	0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	
	0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Helper to read little-endian unsigned short
static inline unsigned short ReadLE16(const unsigned char* ptr) {
	return ptr[0] | (ptr[1] << 8);
}

/*=========================================================================*/
/* Buffer_Put_Pixel -- Puts a pixel on a graphic viewport                  */
/*                                                                         */
/* This is a portable C implementation translated from x86 assembly        */
/*                                                                         */
/* INPUT:                                                                  */
/*   thisptr  -- Pointer to GraphicViewPortClass object                    */
/*   x        -- X position of pixel                                      */
/*   y        -- Y position of pixel                                      */
/*   color    -- Color value to set                                       */
/*                                                                         */
/* OUTPUT:                                                                 */
/*   None                                                                  */
/*=========================================================================*/
extern "C" void Buffer_Put_Pixel(void *thisptr, int x, int y, unsigned char color)
{
	if (!thisptr) return;
	
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	
	// Verify bounds
	if (x < 0 || x >= vp->Get_Width()) return;
	if (y < 0 || y >= vp->Get_Height()) return;

	if (VP_Is_Planar(vp)) {
		GraphicBufferClass *gbp = vp->Get_Graphic_Buffer();
		uint8_t *root = (uint8_t *)gbp->Get_Buffer();
		const int ax = vp->Get_XPos() + x;
		const int ay = vp->Get_YPos() + y;
		const unsigned char c4 = C2P_Map8ToPlanar4(ax, ay, (unsigned char)color);
		ST_Planar_PutPixel(root, GB_ST_Planar_Row_Bytes(gbp), gbp->Get_Width(), gbp->Get_Height(), ax, ay, c4);
		return;
	}

	// Get viewport base pointer (Get_Offset returns pointer value cast to long)
	unsigned char *viewport_base = (unsigned char *)vp->Get_Offset();
	if (!viewport_base) return;
	
	// Calculate row stride (pitch + xadd)
	int row_stride = Get_Row_Stride(vp);
	
	// Calculate pixel pointer using pointer arithmetic
	unsigned char *pixel_ptr = viewport_base + x + y * row_stride;
	
	// Write pixel
	*pixel_ptr = color;
}

/***************************************************************************
 * Fat_Put_Pixel -- Draws a fat pixel (larger than 1x1)                    *
 *                                                                         *
 * INPUT:   x, y - coordinates of upper left corner                        *
 *          color - color value                                            *
 *          siz - size of pixel (square)                                  *
 *          gpage - graphic viewport reference                            *
 *                                                                         *
 * OUTPUT:  none                                                           *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/MiscAsm.cpp (x86 assembly to C)                 *
 *=========================================================================*/
extern "C" void Fat_Put_Pixel(int x, int y, int color, int siz, GraphicViewPortClass &gpage)
{
	if (siz <= 0) return;
	
	// Verify bounds
	if (y < 0 || y >= gpage.Get_Height()) return;
	if (x < 0 || x >= gpage.Get_Width()) return;

	if (VP_Is_Planar(&gpage)) {
		unsigned char c4 = (unsigned char)(color & 15);
		for (int row = 0; row < siz && (y + row) < gpage.Get_Height(); row++) {
			for (int col = 0; col < siz && (x + col) < gpage.Get_Width(); col++) {
				Buffer_Put_Pixel(&gpage, x + col, y + row, c4);
			}
		}
		return;
	}
	
	// Get viewport base pointer (Get_Offset returns pointer value cast to long)
	unsigned char *viewport_base = (unsigned char *)gpage.Get_Offset();
	if (!viewport_base) return;
	
	int row_stride = Get_Row_Stride(&gpage);
	
	// Draw fat pixel (square)
	unsigned char color_byte = (unsigned char)color;
	for (int row = 0; row < siz && (y + row) < gpage.Get_Height(); row++) {
		unsigned char *row_ptr = viewport_base + x + (y + row) * row_stride;
		for (int col = 0; col < siz && (x + col) < gpage.Get_Width(); col++) {
			row_ptr[col] = color_byte;
		}
	}
}

/*=========================================================================*/
/* Buffer_Get_Pixel -- Gets a pixel from a graphic viewport                */
/*                                                                         */
/* This is a portable C implementation translated from x86 assembly        */
/*                                                                         */
/* INPUT:                                                                  */
/*   thisptr  -- Pointer to GraphicViewPortClass object                    */
/*   x        -- X position of pixel                                      */
/*   y        -- Y position of pixel                                      */
/*                                                                         */
/* OUTPUT:                                                                 */
/*   Returns pixel color value, or 0 if out of bounds                      */
/*=========================================================================*/
extern "C" int Buffer_Get_Pixel(void *thisptr, int x, int y)
{
	if (!thisptr) return 0;
	
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	
	// Verify bounds
	if (x < 0 || x >= vp->Get_Width()) return 0;
	if (y < 0 || y >= vp->Get_Height()) return 0;

	if (VP_Is_Planar(vp)) {
		GraphicBufferClass *gbg = vp->Get_Graphic_Buffer();
		const uint8_t *root = (const uint8_t *)gbg->Get_Buffer();
		return (int)ST_Planar_GetPixel(root, GB_ST_Planar_Row_Bytes(gbg), gbg->Get_Width(), gbg->Get_Height(),
			vp->Get_XPos() + x, vp->Get_YPos() + y);
	}
	
	// Get viewport base pointer (Get_Offset returns pointer value cast to long)
	unsigned char *viewport_base = (unsigned char *)vp->Get_Offset();
	if (!viewport_base) return 0;
	
	// Calculate row stride (pitch + xadd)
	int row_stride = Get_Row_Stride(vp);
	
	// Calculate pixel pointer using pointer arithmetic
	unsigned char *pixel_ptr = viewport_base + x + y * row_stride;
	
	// Read pixel
	return (int)*pixel_ptr;
}

/*=========================================================================*/
/* Buffer_Clear -- Clears a buffer to a color                              */
/*=========================================================================*/
extern "C" void Buffer_Clear(void *thisptr, unsigned char color)
{
	if (!thisptr) return;
	
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	
	// Get viewport dimensions
	int width = vp->Get_Width();
	int height = vp->Get_Height();
	if (width <= 0 || height <= 0) return;
	
	if (VP_Is_Planar(vp)) {
		GraphicBufferClass *gb = vp->Get_Graphic_Buffer();
		uint8_t *root = (uint8_t *)gb->Get_Buffer();
		unsigned char c4 = (unsigned char)(color & 15);
		int const rb = GB_ST_Planar_Row_Bytes(gb);
		int const pwb = gb->Get_Width();
		int const phb = gb->Get_Height();
		/*
		 * Full-buffer clear: use same movep/LUT path as C2P (solid ST nibble). Per-pixel PutPixel
		 * works but is slow; sub-rect clears still use PutPixel.
		 */
		if (vp->Get_XPos() == 0 && vp->Get_YPos() == 0
		    && width == pwb && height == phb) {
			ST_Planar_Clear(root, rb, pwb, phb, c4);
			return;
		}
		for (int row = 0; row < height; row++) {
			for (int col = 0; col < width; col++) {
				ST_Planar_PutPixel(root, rb, pwb, phb, vp->Get_XPos() + col, vp->Get_YPos() + row, c4);
			}
		}
		return;
	}

	// Get viewport base pointer (Get_Offset returns pointer value cast to long)
	unsigned char *viewport_base = (unsigned char *)vp->Get_Offset();
	if (!viewport_base) return;
	
	// Calculate row stride (pitch + xadd)
	int row_stride = Get_Row_Stride(vp);
	
	// Clear each row
	for (int row = 0; row < height; row++) {
		unsigned char *row_ptr = viewport_base + row * row_stride;
		memset(row_ptr, color, width);
	}
}

/*=========================================================================*/
/* Buffer_Size_Of_Region -- Gets size of a region                           */
/*=========================================================================*/
extern "C" long Buffer_Size_Of_Region(void *thisptr, int w, int h)
{
	if (!thisptr) return 0;
	
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	return vp->Size_Of_Region(w, h);
}

/*=========================================================================*/
/* Buffer_To_Buffer -- Copies buffer region to another buffer               */
/*=========================================================================*/
extern "C" long Buffer_To_Buffer(void *thisptr, int x, int y, int w, int h, void *buff, long size)
{
	if (!thisptr || !buff) return 0;
	
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	BufferClass *dest_buff = (BufferClass *)buff;
	
	return vp->To_Buffer(x, y, w, h, dest_buff);
}

/*=========================================================================*/
/* Buffer_To_Page -- Copies linear buffer to page/viewport                 */
/*   Buffer is row-major, w bytes per row, h rows.                         */
/*   ST planar: each byte is an ST display nibble 0..15 (same as           */
/*   Buffer_From_Page / ST_Planar_GetPixel). Do not run C2P dither here — */
/*   that path is for 8-bit palette indices (see Buffer_Frame_To_Page).    */
/*=========================================================================*/
extern "C" long Buffer_To_Page(int x, int y, int w, int h, void *Buffer, void *view)
{
	if (!Buffer || !view || w <= 0 || h <= 0) return 0;
	GraphicViewPortClass *vp = (GraphicViewPortClass *)view;
	int vpw = vp->Get_Width();
	int vph = vp->Get_Height();
	if (x + w > vpw || y + h > vph || x < 0 || y < 0) return 0;

	if (VP_Is_Planar(vp)) {
		GraphicBufferClass *gbt = vp->Get_Graphic_Buffer();
		uint8_t *root = (uint8_t *)gbt->Get_Buffer();
		const int ax0 = vp->Get_XPos() + x;
		const int ay0 = vp->Get_YPos() + y;
		const unsigned char *src = (const unsigned char *)Buffer;
		int const rb = GB_ST_Planar_Row_Bytes(gbt);
		int const pwb = gbt->Get_Width();
		int const phb = gbt->Get_Height();
		for (int row = 0; row < h; row++) {
			for (int col = 0; col < w; col++) {
				ST_Planar_PutPixel(root, rb, pwb, phb, ax0 + col, ay0 + row, src[row * w + col]);
			}
		}
		return (long)(w * h);
	}

	unsigned char *base = (unsigned char *)vp->Get_Offset();
	if (!base) return 0;
	int stride = Get_Row_Stride(vp);
	const unsigned char *src = (const unsigned char *)Buffer;
	for (int row = 0; row < h; row++) {
		unsigned char *dest = base + (y + row) * stride + x;
		memcpy(dest, src, (unsigned)w);
		src += w;
	}
	return (long)(w * h);
}

/*=========================================================================*/
/* Buffer_From_Page -- Copies page/viewport rect to linear buffer           */
/*   Buffer must hold at least w*h bytes (row-major).                       */
/*   ST planar: each byte is ST display nibble 0..15 (ST_Planar_GetPixel).  */
/*=========================================================================*/
extern "C" long Buffer_From_Page(int x, int y, int w, int h, void *Buffer, void *view)
{
	if (!Buffer || !view || w <= 0 || h <= 0) return 0;
	GraphicViewPortClass *vp = (GraphicViewPortClass *)view;
	int vpw = vp->Get_Width();
	int vph = vp->Get_Height();
	if (x + w > vpw || y + h > vph || x < 0 || y < 0) return 0;

	if (VP_Is_Planar(vp)) {
		GraphicBufferClass *gbf = vp->Get_Graphic_Buffer();
		const uint8_t *root = (const uint8_t *)gbf->Get_Buffer();
		unsigned char *dest = (unsigned char *)Buffer;
		int const rb = GB_ST_Planar_Row_Bytes(gbf);
		int const pwb = gbf->Get_Width();
		int const phb = gbf->Get_Height();
		for (int row = 0; row < h; row++) {
			for (int col = 0; col < w; col++) {
				dest[row * w + col] = ST_Planar_GetPixel(root, rb, pwb, phb,
					vp->Get_XPos() + x + col, vp->Get_YPos() + y + row);
			}
		}
		return (long)(w * h);
	}

	unsigned char *base = (unsigned char *)vp->Get_Offset();
	if (!base) return 0;
	int stride = Get_Row_Stride(vp);
	unsigned char *dest = (unsigned char *)Buffer;
	for (int row = 0; row < h; row++) {
		unsigned char *src = base + (y + row) * stride + x;
		memcpy(dest, src, (unsigned)w);
		dest += w;
	}
	return (long)(w * h);
}

/*=========================================================================*/
/* Linear_Blit_To_Linear -- Blits between linear buffers                   */
/*=========================================================================*/
extern "C" BOOL Linear_Blit_To_Linear(void *thisptr, void *dest, int x_pixel, int y_pixel, int dx_pixel,
							int dy_pixel, int pixel_width, int pixel_height, BOOL trans)
{
	if (!thisptr || !dest) return FALSE;
	
	GraphicViewPortClass *src_vp = (GraphicViewPortClass *)thisptr;
	GraphicViewPortClass *dest_vp = (GraphicViewPortClass *)dest;
	
	// Get buffer pointers and dimensions
	GraphicBufferClass *src_gb = src_vp->Get_Graphic_Buffer();
	GraphicBufferClass *dest_gb = dest_vp->Get_Graphic_Buffer();
	if (!src_gb || !dest_gb) return FALSE;
	
	unsigned char *src_base = (unsigned char *)src_vp->Get_Offset();
	unsigned char *dest_base = (unsigned char *)dest_vp->Get_Offset();
	if (!src_gb->Get_Buffer() || !dest_gb->Get_Buffer()) return FALSE;
	
	int src_stride = Get_Row_Stride(src_vp);
	int dest_stride = Get_Row_Stride(dest_vp);
	const int src_planar = VP_Is_Planar(src_vp) ? 1 : 0;
	const int dst_planar = VP_Is_Planar(dest_vp) ? 1 : 0;

	if (!src_planar && !dst_planar) {
		unsigned char *src_ptr = src_base + x_pixel + y_pixel * src_stride;
		unsigned char *dest_ptr = dest_base + dx_pixel + dy_pixel * dest_stride;
		if (trans) {
			for (int y = 0; y < pixel_height; y++) {
				for (int x = 0; x < pixel_width; x++) {
					unsigned char pixel = src_ptr[x];
					if (pixel != 0)
						dest_ptr[x] = pixel;
				}
				src_ptr += src_stride;
				dest_ptr += dest_stride;
			}
		} else {
			/*
			** Scroll blits often copy within the same surface with overlap. memcpy is
			** undefined for overlap and can smear/leave holes; use memmove and choose
			** row order for vertical overlap safety.
			*/
			const bool same_surface = (src_base == dest_base);
			if (same_surface && dest_ptr > src_ptr && dy_pixel > y_pixel) {
				for (int y = pixel_height - 1; y >= 0; --y) {
					unsigned char *s = src_base + x_pixel + (y_pixel + y) * src_stride;
					unsigned char *d = dest_base + dx_pixel + (dy_pixel + y) * dest_stride;
					memmove(d, s, (size_t)pixel_width);
				}
			} else {
				for (int y = 0; y < pixel_height; y++) {
					memmove(dest_ptr, src_ptr, (size_t)pixel_width);
					src_ptr += src_stride;
					dest_ptr += dest_stride;
				}
			}
		}
		return TRUE;
	}

	const uint8_t *src_root = GB_Uses_ST_Planar_Surface(src_gb) ? (const uint8_t *)src_gb->Get_Buffer() : NULL;
	uint8_t *dst_root = GB_Uses_ST_Planar_Surface(dest_gb) ? (uint8_t *)dest_gb->Get_Buffer() : NULL;

	/*
	** Fast linear -> planar path: use bulk C2P conversion instead of per-pixel
	** ST_Planar_PutPixel writes in the generic loop below.
	*/
	if (!src_planar && dst_planar && !trans && dst_root) {
		const int dst_x0 = dest_vp->Get_XPos() + dx_pixel;
		const int dst_y0 = dest_vp->Get_YPos() + dy_pixel;
		const int dst_w = dest_gb->Get_Width();
		const int dst_h = dest_gb->Get_Height();
		if (dst_x0 >= 0 && dst_y0 >= 0
			&& dst_x0 + pixel_width <= dst_w
			&& dst_y0 + pixel_height <= dst_h) {
			const uint8_t *logical = (const uint8_t *)src_base
				+ (size_t)y_pixel * (size_t)src_stride
				+ (size_t)x_pixel;
			C2P_Render_Logical_To_Planar_Rect(
				logical,
				pixel_width,
				pixel_height,
				src_stride,
				dst_root,
				dest_stride,
				dst_w,
				dst_h,
				dst_x0,
				dst_y0,
				dst_x0,
				dst_y0);
			return TRUE;
		}
	}

	/*
	** ST planar self-blit fast path: hardware blitter with skew/masks
	** (see st_blitter_blit.cpp).
	*/
	if (src_planar && dst_planar && !trans
		&& src_gb == dest_gb
		&& src_root && dst_root
		&& AllowHardwareBlitFills) {
		const int sx_abs = src_vp->Get_XPos() + x_pixel;
		const int sy_abs = src_vp->Get_YPos() + y_pixel;
		const int dx_abs = dest_vp->Get_XPos() + dx_pixel;
		const int dy_abs = dest_vp->Get_YPos() + dy_pixel;
		const int src_bpl = GB_ST_Planar_Row_Bytes(src_gb);
		const int dst_bpl = GB_ST_Planar_Row_Bytes(dest_gb);
		if (ST_Blitter_Planar_Rect_Blit(
				src_root,
				src_bpl,
				src_gb->Get_Width(),
				src_gb->Get_Height(),
				sx_abs,
				sy_abs,
				dst_root,
				dst_bpl,
				dest_gb->Get_Width(),
				dest_gb->Get_Height(),
				dx_abs,
				dy_abs,
				pixel_width,
				pixel_height)) {
			return TRUE;
		}
	}

	/* Full-buffer planar -> planar: byte-identical copy (same layout as C2P / Setscreen). */
	{
		const int src_bpl = GB_ST_Planar_Row_Bytes(src_gb);
		const int dst_bpl = GB_ST_Planar_Row_Bytes(dest_gb);
		const long src_bytes = (long)src_bpl * (long)src_gb->Get_Height();
		if (src_planar && dst_planar && !trans
			&& pixel_width == src_gb->Get_Width() && pixel_height == src_gb->Get_Height()
			&& src_gb->Get_Width() == dest_gb->Get_Width()
			&& src_gb->Get_Height() == dest_gb->Get_Height()
			&& src_bpl == dst_bpl
			&& x_pixel == 0 && y_pixel == 0 && dx_pixel == 0 && dy_pixel == 0
			&& src_vp->Get_XPos() == 0 && src_vp->Get_YPos() == 0
			&& dest_vp->Get_XPos() == 0 && dest_vp->Get_YPos() == 0
			&& src_root && dst_root
			&& src_bytes == (long)dst_bpl * (long)dest_gb->Get_Height()) {
			memcpy(dst_root, src_root, (size_t)src_bytes);
			return TRUE;
		}
	}

	for (int y = 0; y < pixel_height; y++) {
		for (int x = 0; x < pixel_width; x++) {
			unsigned char pixel;
			if (src_planar) {
				pixel = ST_Planar_GetPixel(src_root,
					GB_ST_Planar_Row_Bytes(src_gb),
					src_gb->Get_Width(),
					src_gb->Get_Height(),
					src_vp->Get_XPos() + x_pixel + x,
					src_vp->Get_YPos() + y_pixel + y);
			} else {
				pixel = src_base[(y_pixel + y) * src_stride + x_pixel + x];
			}
			if (trans && pixel == 0)
				continue;
			if (dst_planar) {
				const int ax = dest_vp->Get_XPos() + dx_pixel + x;
				const int ay = dest_vp->Get_YPos() + dy_pixel + y;
				unsigned char c4 = (unsigned char)(src_planar ? (pixel & 15)
					: C2P_Map8ToPlanar4(ax, ay, pixel));
				ST_Planar_PutPixel(dst_root,
					GB_ST_Planar_Row_Bytes(dest_gb),
					dest_gb->Get_Width(),
					dest_gb->Get_Height(),
					ax, ay, c4);
			} else {
				dest_base[(dy_pixel + y) * dest_stride + dx_pixel + x] = pixel;
			}
		}
	}

	return TRUE;
}

/*=========================================================================*/
/* Linear_Scale_To_Linear -- Scales between linear buffers                 */
/*   Nearest-neighbor scale from source rect to destination rect.          */
/*=========================================================================*/
extern "C" BOOL Linear_Scale_To_Linear(void *src, void *dest, int src_x, int src_y, int dst_x, int dst_y,
							int src_w, int src_h, int dst_w, int dst_h, BOOL trans, char *remap)
{
	if (!src || !dest || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) return FALSE;
	
	GraphicViewPortClass *src_vp = (GraphicViewPortClass *)src;
	GraphicViewPortClass *dest_vp = (GraphicViewPortClass *)dest;
	
	GraphicBufferClass *src_gb = src_vp->Get_Graphic_Buffer();
	GraphicBufferClass *dest_gb = dest_vp->Get_Graphic_Buffer();
	if (!src_gb || !dest_gb) return FALSE;
	
	unsigned char *src_base = (unsigned char *)src_vp->Get_Offset();
	unsigned char *dest_base = (unsigned char *)dest_vp->Get_Offset();
	if (!src_gb->Get_Buffer() || !dest_gb->Get_Buffer()) return FALSE;
	
	int src_stride = Get_Row_Stride(src_vp);
	int dest_stride = Get_Row_Stride(dest_vp);
	const int src_planar = VP_Is_Planar(src_vp) ? 1 : 0;
	const int dst_planar = VP_Is_Planar(dest_vp) ? 1 : 0;
	const uint8_t *src_root = GB_Uses_ST_Planar_Surface(src_gb) ? (const uint8_t *)src_gb->Get_Buffer() : NULL;
	uint8_t *dst_root = GB_Uses_ST_Planar_Surface(dest_gb) ? (uint8_t *)dest_gb->Get_Buffer() : NULL;

	// Nearest-neighbor scale: for each dest pixel, sample source
	for (int dy = 0; dy < dst_h; dy++) {
		int sy = (dst_h > 1 && src_h > 1) ? (dy * (src_h - 1) / (dst_h - 1)) : 0;
		unsigned char *src_row = src_base + (src_y + sy) * src_stride + src_x;
		unsigned char *dest_row = dest_base + (dst_y + dy) * dest_stride + dst_x;
		
		for (int dx = 0; dx < dst_w; dx++) {
			int sx = (dst_w > 1 && src_w > 1) ? (dx * (src_w - 1) / (dst_w - 1)) : 0;
			unsigned char pixel;
			if (src_planar) {
				pixel = ST_Planar_GetPixel(src_root,
					GB_ST_Planar_Row_Bytes(src_gb),
					src_gb->Get_Width(),
					src_gb->Get_Height(),
					src_vp->Get_XPos() + src_x + sx,
					src_vp->Get_YPos() + src_y + sy);
			} else {
				pixel = src_row[sx];
			}
			if (trans && pixel == 0)
				continue;
			unsigned char out = (remap ? (unsigned char)remap[(unsigned char)pixel] : pixel);
			if (dst_planar) {
				const int ax = dest_vp->Get_XPos() + dst_x + dx;
				const int ay = dest_vp->Get_YPos() + dst_y + dy;
				unsigned char c4 = (unsigned char)(src_planar ? (out & 15)
					: C2P_Map8ToPlanar4(ax, ay, out));
				ST_Planar_PutPixel(dst_root,
					GB_ST_Planar_Row_Bytes(dest_gb),
					dest_gb->Get_Width(),
					dest_gb->Get_Height(),
					ax, ay, c4);
			} else {
				dest_row[dx] = out;
			}
		}
	}
	
	return TRUE;
}

/*=========================================================================*/
/* Buffer_Print -- Prints text to a buffer                                  */
/*=========================================================================*/
extern "C" LONG Buffer_Print(void *thisptr, const char *str, int x, int y, int fcolor, int bcolor)
{
	if (!thisptr || !str || !FontPtr) return 0;
	
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	
	// Get viewport dimensions
	int vpwidth = vp->Get_Width();
	int vpheight = vp->Get_Height();
	if (vpwidth <= 0 || vpheight <= 0) return 0;
	
	// Calculate buffer width (pitch + xadd)
	int bufferwidth = Get_Row_Stride(vp);
	
	unsigned char *viewport_base = (unsigned char *)vp->Get_Offset();
	if (!VP_Is_Planar(vp) && !viewport_base) return 0;
	
	// Set up color translation table
	ColorXlat[0] = (unsigned char)bcolor;
	ColorXlat[1] = (unsigned char)fcolor;
	ColorXlat[16] = (unsigned char)fcolor;
	
	// Get font structure pointers
	const unsigned char *font_bytes = (const unsigned char *)FontPtr;
	unsigned short info_block_offset = ReadLE16(font_bytes + FONTINFOBLOCK);
	unsigned short offset_block_offset = ReadLE16(font_bytes + FONTOFFSETBLOCK);
	unsigned short width_block_offset = ReadLE16(font_bytes + FONTWIDTHBLOCK);
	unsigned short height_block_offset = ReadLE16(font_bytes + FONTHEIGHTBLOCK);
	
	const unsigned char *infoblock = font_bytes + info_block_offset;
	const unsigned char *offsetblock = font_bytes + offset_block_offset;
	const unsigned char *widthblock = font_bytes + width_block_offset;
	const unsigned char *heightblock = font_bytes + height_block_offset;
	
	// Get max height from info block
	unsigned char maxheight = infoblock[FONTINFOMAXHEIGHT];
	
	// Check if text will fit vertically
	if (y + maxheight > (unsigned)vpheight) return 0;
	
	// Current position
	int cur_x = x;
	int cur_y = y;
	int original_x = x;
	
	// Calculate starting position in buffer (linear layout only)
	unsigned char *curline = NULL;
	unsigned char *startdraw = NULL;
	if (!VP_Is_Planar(vp)) {
		curline = viewport_base + cur_y * bufferwidth;
		startdraw = curline + cur_x;
	} else {
		startdraw = (unsigned char *)vp->Get_Graphic_Buffer()->Get_Buffer();
	}
	
	// Process each character
	const char *string = str;
	while (*string) {
		unsigned char ch = (unsigned char)*string++;
		
		// Handle line feed (LF = 10) or carriage return (CR = 13)
		if (ch == 10 || ch == 13) {
			cur_y += maxheight + FontYSpacing;
			if (cur_y + maxheight > (unsigned)vpheight) break;
			
			if (!VP_Is_Planar(vp))
				curline = viewport_base + cur_y * bufferwidth;
			
			// CR returns to original x, LF goes to x=0
			if (ch == 13) {
				cur_x = original_x;
			} else {
				cur_x = 0;
			}
			
			if (!VP_Is_Planar(vp))
				startdraw = curline + cur_x;
			continue;
		}
		
		// Get character width
		unsigned char charwidth = widthblock[ch];
		int next_x = cur_x + charwidth + FontXSpacing;
		
		// Check if character fits horizontally
		if (next_x > vpwidth) {
			// Force line feed
			string--;  // Back up to re-process this character
			cur_y += maxheight + FontYSpacing;
			if (cur_y + maxheight > (unsigned)vpheight) break;
			
			if (!VP_Is_Planar(vp))
				curline = viewport_base + cur_y * bufferwidth;
			cur_x = original_x;
			if (!VP_Is_Planar(vp))
				startdraw = curline + cur_x;
			continue;
		}
		
		// Get character data offset
		unsigned short char_offset = ReadLE16(offsetblock + (ch * 2));
		const unsigned char *chardata = font_bytes + char_offset;
		
		// Get character height info
		const unsigned char *charheight_ptr = heightblock + (ch * 2);
		unsigned char topblank = charheight_ptr[0];
		unsigned char charheight = charheight_ptr[1];
		unsigned char bottomblank = maxheight - (topblank + charheight);
		
		unsigned char *draw_ptr = startdraw;
		
		// Draw top blank area
		if (topblank > 0) {
			unsigned char bgcolor = ColorXlat[0];
			if (bgcolor != 0) {  // Not transparent
				for (unsigned char row = 0; row < topblank; row++) {
					for (unsigned char col = 0; col < charwidth; col++) {
						if (cur_x + col < (unsigned)vpwidth && cur_y + row < (unsigned)vpheight) {
							if (VP_Is_Planar(vp))
								Buffer_Put_Pixel(vp, cur_x + col, cur_y + row, bgcolor);
							else {
								unsigned char *row_ptr = draw_ptr;
								row_ptr[col] = bgcolor;
							}
						}
					}
					if (!VP_Is_Planar(vp))
						draw_ptr += bufferwidth;
				}
			} else {
				if (!VP_Is_Planar(vp))
					draw_ptr += topblank * bufferwidth;
			}
		}
		
		// Draw character data
		if (charheight > 0) {
			const unsigned char *data_ptr = chardata;
			for (unsigned char row = 0; row < charheight; row++) {
				unsigned char col = 0;
				unsigned char remaining_width = charwidth;
				
				while (remaining_width > 0) {
					// Read a byte containing 2 pixels
					unsigned char data_byte = *data_ptr++;
					
					// Process low nibble (first pixel)
					unsigned char pixel = data_byte & 0x0F;
					unsigned char color = ColorXlat[pixel];
					if (cur_x + col < (unsigned)vpwidth && cur_y + topblank + row < (unsigned)vpheight) {
						if (color != 0) {  // Not transparent
							if (VP_Is_Planar(vp))
								Buffer_Put_Pixel(vp, cur_x + col, cur_y + topblank + row, color);
							else {
								unsigned char *row_ptr = draw_ptr;
								row_ptr[col] = color;
							}
						}
					}
					col++;
					remaining_width--;
					
					// Process high nibble (second pixel) if width remaining
					if (remaining_width > 0) {
						pixel = (data_byte >> 4) & 0x0F;
						color = ColorXlat[pixel];
						if (cur_x + col < (unsigned)vpwidth && cur_y + topblank + row < (unsigned)vpheight) {
							if (color != 0) {  // Not transparent
								if (VP_Is_Planar(vp))
									Buffer_Put_Pixel(vp, cur_x + col, cur_y + topblank + row, color);
								else {
									unsigned char *row_ptr = draw_ptr;
									row_ptr[col] = color;
								}
							}
						}
						col++;
						remaining_width--;
					}
				}
				
				if (!VP_Is_Planar(vp))
					draw_ptr += bufferwidth;
			}
		}
		
		// Draw bottom blank area
		if (bottomblank > 0) {
			unsigned char bgcolor = ColorXlat[0];
			if (bgcolor != 0) {  // Not transparent
				for (unsigned char row = 0; row < bottomblank; row++) {
					for (unsigned char col = 0; col < charwidth; col++) {
						if (cur_x + col < (unsigned)vpwidth && cur_y + topblank + charheight + row < (unsigned)vpheight) {
							if (VP_Is_Planar(vp))
								Buffer_Put_Pixel(vp, cur_x + col, cur_y + topblank + charheight + row, bgcolor);
							else {
								unsigned char *row_ptr = draw_ptr;
								row_ptr[col] = bgcolor;
							}
						}
					}
					if (!VP_Is_Planar(vp))
						draw_ptr += bufferwidth;
				}
			}
		}
		
		// Update position for next character
		cur_x = next_x;
		if (!VP_Is_Planar(vp)) {
			curline = viewport_base + cur_y * bufferwidth;
			startdraw = curline + cur_x;
		}
	}
	
	// Return pointer to next draw position (cast to long for compatibility)
	return (LONG)(startdraw ? startdraw : viewport_base);
}

/*=========================================================================*/
/* Buffer_Draw_Line -- Draws a line on a buffer                             */
/*=========================================================================*/
extern "C" VOID Buffer_Draw_Line(void *thisptr, int sx, int sy, int dx, int dy, unsigned char color)
{
	if (!thisptr) return;
	
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	
	// Get viewport dimensions
	int width = vp->Get_Width();
	int height = vp->Get_Height();
	if (width <= 0 || height <= 0) return;

	// Reject lines that are completely outside the viewport
	if (sx < 0 && dx < 0 || sy < 0 && dy < 0 || sx >= width && dx >= width || sy >= height && dy >= height) return;
	
	// Clip coordinates to viewport bounds
	if (sx < 0) sx = 0;
	if (sy < 0) sy = 0;
	if (dx < 0) dx = 0;
	if (dy < 0) dy = 0;
	if (sx >= width) sx = width - 1;
	if (sy >= height) sy = height - 1;
	if (dx >= width) dx = width - 1;
	if (dy >= height) dy = height - 1;

	/* Planar special-cases: route pure H/V lines to the optimized span drawers. */
	if (VP_Is_Planar(vp) && (sx == dx || sy == dy)) {
		GraphicBufferClass *gb = vp->Get_Graphic_Buffer();
		uint16_t *root = (gb && GB_Uses_ST_Planar_Surface(gb)) ? (uint16_t *)gb->Get_Buffer() : NULL;
		const int planar_w = gb ? gb->Get_Width() : 0;
		const int planar_h = gb ? gb->Get_Height() : 0;
		const short planar_row_words = (short)(Get_Row_Stride(vp) >> 1);
		const int x1_abs = vp->Get_XPos() + sx;
		const int y1_abs = vp->Get_YPos() + sy;
		const int x2_abs = vp->Get_XPos() + dx;
		const int y2_abs = vp->Get_YPos() + dy;
		const unsigned char color4 = C2P_Map8ToNearest4(color);

		if (root
			&& planar_row_words > 0
			&& planar_w > 0 && planar_h > 0
			&& x1_abs >= 0 && y1_abs >= 0
			&& x2_abs >= 0 && y2_abs >= 0
			&& x1_abs < planar_w && y1_abs < planar_h
			&& x2_abs < planar_w && y2_abs < planar_h) {
			if (sy == dy) {
				short x1 = (short)x1_abs;
				short x2 = (short)x2_abs;
				if (x1 > x2) {
					short t = x1;
					x1 = x2;
					x2 = t;
				}
				ST_Planar_Draw_HLine_Fast(root, planar_row_words, (short)y1_abs, x1, x2, color4);
				return;
			}
			if (sx == dx) {
				short y1 = (short)y1_abs;
				short y2 = (short)y2_abs;
				if (y1 > y2) {
					short t = y1;
					y1 = y2;
					y2 = t;
				}
				ST_Planar_Draw_VLine_Fast(root, planar_row_words, (short)x1_abs, y1, y2, color4);
				return;
			}
		}
	}
	
	// Simple line drawing using Bresenham's algorithm
	int x0 = sx, y0 = sy, x1 = dx, y1 = dy;
	int dx_abs = (x1 > x0) ? (x1 - x0) : (x0 - x1);
	int dy_abs = (y1 > y0) ? (y1 - y0) : (y0 - y1);
	int x_inc = (x1 > x0) ? 1 : -1;
	int y_inc = (y1 > y0) ? 1 : -1;
	
	int x = x0, y = y0;
	
	if (dx_abs >= dy_abs) {
		// More horizontal than vertical
		int error = dx_abs / 2;
		for (int i = 0; i <= dx_abs; i++) {
			if (x >= 0 && x < width && y >= 0 && y < height) {
				Buffer_Put_Pixel(vp, x, y, color);
			}
			error -= dy_abs;
			if (error < 0) {
				y += y_inc;
				error += dx_abs;
			}
			x += x_inc;
		}
	} else {
		// More vertical than horizontal
		int error = dy_abs / 2;
		for (int i = 0; i <= dy_abs; i++) {
			if (x >= 0 && x < width && y >= 0 && y < height) {
				Buffer_Put_Pixel(vp, x, y, color);
			}
			error -= dx_abs;
			if (error < 0) {
				x += x_inc;
				error += dy_abs;
			}
			y += y_inc;
		}
	}
}

/*=========================================================================*/
/* Buffer_Draw_Rect -- Draws a rectangle on a buffer                       */
/*=========================================================================*/
extern "C" VOID Buffer_Draw_Rect(void *thisptr, int sx, int sy, int dx, int dy, unsigned char color)
{
	if (!thisptr) return;
	
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	// Clip coordinates to viewport bounds
	int width = vp->Get_Width();
	int height = vp->Get_Height();

	// Accept either corner order, matching Win32 Draw_Rect behavior.
	if (sx > dx) {
		int t = sx;
		sx = dx;
		dx = t;
	}
	if (sy > dy) {
		int t = sy;
		sy = dy;
		dy = t;
	}

	enum {
		RECT_EDGE_LEFT   = 1 << 0,
		RECT_EDGE_TOP    = 1 << 1,
		RECT_EDGE_RIGHT  = 1 << 2,
		RECT_EDGE_BOTTOM = 1 << 3
	};
	unsigned char edge_flags = (unsigned char)(RECT_EDGE_LEFT | RECT_EDGE_TOP | RECT_EDGE_RIGHT | RECT_EDGE_BOTTOM);

	if (sx < 0) {
		sx = 0;
		edge_flags = (unsigned char)(edge_flags & ~RECT_EDGE_LEFT);
	}
	if (sy < 0) {
		sy = 0;
		edge_flags = (unsigned char)(edge_flags & ~RECT_EDGE_TOP);
	}
	if (dx >= width) {
		dx = width - 1;
		edge_flags = (unsigned char)(edge_flags & ~RECT_EDGE_RIGHT);
	}
	if (dy >= height) {
		dy = height - 1;
		edge_flags = (unsigned char)(edge_flags & ~RECT_EDGE_BOTTOM);
	}
	if (!edge_flags) return;

	// Check if rectangle collapsed after viewport-local clipping.
	if (sx > dx || sy > dy) return;

	/*
	**	Second-stage clip in buffer-absolute coordinates. This handles viewports
	**	that are only partially inside their backing buffer.
	*/
	GraphicBufferClass *gb = vp->Get_Graphic_Buffer();
	const int buffer_w = gb ? gb->Get_Width() : 0;
	const int buffer_h = gb ? gb->Get_Height() : 0;
	if (buffer_w <= 0 || buffer_h <= 0) return;
	int x1_abs = vp->Get_XPos() + sx;
	int y1_abs = vp->Get_YPos() + sy;
	int x2_abs = vp->Get_XPos() + dx;
	int y2_abs = vp->Get_YPos() + dy;
	if (x1_abs >= buffer_w) {
		edge_flags = (unsigned char)(edge_flags & ~RECT_EDGE_LEFT);
	}
	if (x1_abs < 0) {
		x1_abs = 0;
		edge_flags = (unsigned char)(edge_flags & ~RECT_EDGE_LEFT);
	}
	if (y1_abs >= buffer_h) {
		edge_flags = (unsigned char)(edge_flags & ~RECT_EDGE_TOP);
	}
	if (y1_abs < 0) {
		y1_abs = 0;
		edge_flags = (unsigned char)(edge_flags & ~RECT_EDGE_TOP);
	}
	if (x2_abs < 0) {
		edge_flags = (unsigned char)(edge_flags & ~RECT_EDGE_RIGHT);
	}
	if (x2_abs >= buffer_w) {
		x2_abs = buffer_w - 1;
		edge_flags = (unsigned char)(edge_flags & ~RECT_EDGE_RIGHT);
	}
	if (y2_abs < 0) {
		edge_flags = (unsigned char)(edge_flags & ~RECT_EDGE_BOTTOM);
	}
	if (y2_abs >= buffer_h) {
		y2_abs = buffer_h - 1;
		edge_flags = (unsigned char)(edge_flags & ~RECT_EDGE_BOTTOM);
	}
	if (x2_abs < 0) x2_abs = 0;
	if (y2_abs < 0) y2_abs = 0;
	if (x1_abs >= buffer_w) x1_abs = buffer_w - 1;
	if (y1_abs >= buffer_h) y1_abs = buffer_h - 1;
	if (!edge_flags) return;
	
	if (VP_Is_Planar(vp)) {
		/* Color is an ST 4-bit index (0..15). */
		uint16_t *root = (gb && GB_Uses_ST_Planar_Surface(gb)) ? (uint16_t *)gb->Get_Buffer() : NULL;
		const int planar_row_bytes = Get_Row_Stride(vp);
		const short planar_row_words = (short)(planar_row_bytes >> 1);
		const int planar_w = gb ? gb->Get_Width() : 0;
		const int planar_h = gb ? gb->Get_Height() : 0;

		if (root
			&& planar_row_bytes > 0
			&& planar_w > 0 && planar_h > 0
			&& x1_abs >= 0 && y1_abs >= 0
			&& x2_abs < planar_w && y2_abs < planar_h) {
			unsigned char color4 = C2P_Map8ToNearest4(color);
			if (edge_flags & RECT_EDGE_TOP) {
				ST_Planar_Draw_HLine_Fast(root, planar_row_words, (short)y1_abs, (short)x1_abs, (short)x2_abs, color4);
			}
			if ((edge_flags & RECT_EDGE_BOTTOM) && y2_abs != y1_abs) {
				ST_Planar_Draw_HLine_Fast(root, planar_row_words, (short)y2_abs, (short)x1_abs, (short)x2_abs, color4);
			}
			if (edge_flags & RECT_EDGE_LEFT) {
				ST_Planar_Draw_VLine_Fast(root, planar_row_words, (short)x1_abs, (short)y1_abs, (short)y2_abs, color4);
			}
			if ((edge_flags & RECT_EDGE_RIGHT) && x2_abs != x1_abs) {
				ST_Planar_Draw_VLine_Fast(root, planar_row_words, (short)x2_abs, (short)y1_abs, (short)y2_abs, color4);
			}
		}
		return;
	}

	/* Convert back to viewport-local for generic branch below. */
	sx = x1_abs - vp->Get_XPos();
	sy = y1_abs - vp->Get_YPos();
	dx = x2_abs - vp->Get_XPos();
	dy = y2_abs - vp->Get_YPos();

	// Get viewport base pointer (Get_Offset returns pointer value cast to long)
	unsigned char *viewport_base = (unsigned char *)vp->Get_Offset();
	if (!viewport_base) return;
	
	// Calculate row stride (pitch + xadd)
	int row_stride = Get_Row_Stride(vp);
	
	// Draw top and bottom horizontal lines
	int rect_width = dx - sx + 1;
	for (int x = 0; x < rect_width; x++) {
		if (sx + x < width) {
			unsigned char *top_ptr = viewport_base + (sx + x) + sy * row_stride;
			unsigned char *bottom_ptr = viewport_base + (sx + x) + dy * row_stride;
			*top_ptr = color;
			*bottom_ptr = color;
		}
	}
	
	// Draw left and right vertical lines
	int rect_height = dy - sy + 1;
	for (int y = 0; y < rect_height; y++) {
		if (sy + y < height) {
			unsigned char *left_ptr = viewport_base + sx + (sy + y) * row_stride;
			unsigned char *right_ptr = viewport_base + dx + (sy + y) * row_stride;
			*left_ptr = color;
			*right_ptr = color;
		}
	}
}

/*=========================================================================*/
/* Buffer_Fill_Rect -- Fills a rectangle on a buffer                       */
/*=========================================================================*/
extern "C" VOID Buffer_Fill_Rect(void *thisptr, int sx, int sy, int dx, int dy, unsigned char color)
{
	if (!thisptr) return;
	
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	
	// Clip coordinates to viewport bounds
	int width = vp->Get_Width();
	int height = vp->Get_Height();
	if (sx < 0) sx = 0;
	if (sy < 0) sy = 0;
	if (dx >= width) dx = width - 1;
	if (dy >= height) dy = height - 1;
	
	// Check if rectangle is valid
	if (sx > dx || sy > dy) return;
	if (sx >= width || sy >= height || dx < 0 || dy < 0) return;
	
	if (VP_Is_Planar(vp)) {
		/* Full 8-bit palette index — Buffer_Put_Pixel runs C2P_Map8ToPlanar4 (do not mask to 4). */
		unsigned char palidx = (unsigned char)color;
		const int ax = vp->Get_XPos() + sx;
		const int ay = vp->Get_YPos() + sy;
		const int rect_width = dx - sx + 1;
		const int rect_height = dy - sy + 1;
		GraphicBufferClass *gb = vp->Get_Graphic_Buffer();
		uint8_t *root = gb ? (uint8_t *)gb->Get_Buffer() : NULL;
		const int planar_width = gb ? gb->Get_Width() : 0;
		const int planar_height = gb ? gb->Get_Height() : 0;
		const int planar_row_bytes = Get_Row_Stride(vp);
		if (root
			&& planar_row_bytes > 0
			&& ax >= 0
			&& ay >= 0
			&& ax + rect_width <= planar_width
			&& ay + rect_height <= planar_height) {
			/*
			 * Flat ST color: weight row has a single non-zero entry, so dither is constant.
			 * ST_Planar_Fill_Rect_Fast shares the hline span logic but computes masks/fills once.
			 */
			uint8_t flat_color4 = 0;
			if (C2P_Is_Palette_Index_Clean4(palidx, &flat_color4)) {
				uint16_t *planar_root = (uint16_t *)root;
				const short planar_row_words = (short)(planar_row_bytes >> 1);
				ST_Planar_Fill_Rect_Fast(
					planar_root,
					planar_row_words,
					(short)ay,
					(short)(ay + rect_height - 1),
					(short)ax,
					(short)(ax + rect_width - 1),
					(uint16_t)flat_color4);
				return;
			}
			const int lead = (ax & 7) ? MIN(8 - (ax & 7), rect_width) : 0;
			const int middle_width = ((rect_width - lead) / 8) * 8;
			const int middle_sx = sx + lead;
			const int tail_sx = middle_sx + middle_width;

			if (middle_width > 0) {
				for (int row = sy; row <= dy; row++) {
					for (int col = sx; col < middle_sx; col++) {
						Buffer_Put_Pixel(vp, col, row, palidx);
					}
					for (int col = tail_sx; col <= dx; col++) {
						Buffer_Put_Pixel(vp, col, row, palidx);
					}
				}
				C2P_Fill_Aligned8_Rect(
					root,
					planar_row_bytes,
					planar_width,
					planar_height,
					ax + lead,
					ay,
					middle_width,
					rect_height,
					palidx);
				return;
			}
		}
		for (int row = sy; row <= dy; row++) {
			for (int col = sx; col <= dx; col++) {
				Buffer_Put_Pixel(vp, col, row, palidx);
			}
		}
		return;
	}

	// Get viewport base pointer (Get_Offset returns pointer value cast to long)
	unsigned char *viewport_base = (unsigned char *)vp->Get_Offset();
	if (!viewport_base) return;
	
	// Calculate dimensions
	int rect_width = dx - sx + 1;
	int rect_height = dy - sy + 1;
	
	// Calculate row stride (pitch + xadd)
	int row_stride = Get_Row_Stride(vp);
	
	// Fill each row
	for (int row = 0; row < rect_height; row++) {
		unsigned char *row_ptr = viewport_base + sx + (sy + row) * row_stride;
		memset(row_ptr, color, rect_width);
	}
}

/*=========================================================================*/
/* Buffer_Remap -- Remaps colors in a buffer region                        */
/*=========================================================================*/
extern "C" VOID Buffer_Remap(void *thisptr, int sx, int sy, int width, int height, void *remap)
{
	if (!thisptr || !remap || width <= 0 || height <= 0) {
		return;
	}

	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	const unsigned char *map = (const unsigned char *)remap;

	const int vpw = vp->Get_Width();
	const int vph = vp->Get_Height();
	if (vpw <= 0 || vph <= 0) {
		return;
	}

	int x0 = sx;
	int y0 = sy;
	int x1 = sx + width - 1;
	int y1 = sy + height - 1;

	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 >= vpw) x1 = vpw - 1;
	if (y1 >= vph) y1 = vph - 1;
	if (x0 > x1 || y0 > y1) {
		return;
	}

	for (int y = y0; y <= y1; ++y) {
		for (int x = x0; x <= x1; ++x) {
			unsigned char src = (unsigned char)Buffer_Get_Pixel(vp, x, y);
			Buffer_Put_Pixel(vp, x, y, map[src]);
		}
	}
}

/*=========================================================================*/
/* Buffer_Fill_Quad -- Fills a quadrilateral on a buffer                   */
/*=========================================================================*/
static inline int Edge_Function(int ax, int ay, int bx, int by, int px, int py)
{
	return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static void Fill_Triangle_Solid(
	GraphicViewPortClass *vp,
	int x0, int y0,
	int x1, int y1,
	int x2, int y2,
	unsigned char color)
{
	int min_x = x0;
	int max_x = x0;
	int min_y = y0;
	int max_y = y0;

	if (x1 < min_x) min_x = x1;
	if (x2 < min_x) min_x = x2;
	if (x1 > max_x) max_x = x1;
	if (x2 > max_x) max_x = x2;
	if (y1 < min_y) min_y = y1;
	if (y2 < min_y) min_y = y2;
	if (y1 > max_y) max_y = y1;
	if (y2 > max_y) max_y = y2;

	const int vpw = vp->Get_Width();
	const int vph = vp->Get_Height();
	if (vpw <= 0 || vph <= 0) {
		return;
	}

	if (min_x < 0) min_x = 0;
	if (min_y < 0) min_y = 0;
	if (max_x >= vpw) max_x = vpw - 1;
	if (max_y >= vph) max_y = vph - 1;
	if (min_x > max_x || min_y > max_y) {
		return;
	}

	int area = Edge_Function(x0, y0, x1, y1, x2, y2);
	if (area == 0) {
		return;
	}
	if (area < 0) {
		int tx = x1; x1 = x2; x2 = tx;
		int ty = y1; y1 = y2; y2 = ty;
	}

	for (int y = min_y; y <= max_y; ++y) {
		for (int x = min_x; x <= max_x; ++x) {
			const int w0 = Edge_Function(x1, y1, x2, y2, x, y);
			const int w1 = Edge_Function(x2, y2, x0, y0, x, y);
			const int w2 = Edge_Function(x0, y0, x1, y1, x, y);
			if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
				Buffer_Put_Pixel(vp, x, y, color);
			}
		}
	}
}

extern "C" VOID Buffer_Fill_Quad(void *thisptr, VOID *span_buff, int x0, int y0, int x1, int y1,
						int x2, int y2, int x3, int y3, int color)
{
	(void)span_buff;
	if (!thisptr) return;

	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	const unsigned char fill = (unsigned char)color;

	/* Split quad into two triangles and rasterize directly. */
	Fill_Triangle_Solid(vp, x0, y0, x1, y1, x2, y2, fill);
	Fill_Triangle_Solid(vp, x0, y0, x2, y2, x3, y3, fill);
}

/*=========================================================================*/
/* Buffer_Draw_Stamp -- Draws a stamp/icon on a buffer                     */
/*=========================================================================*/
extern "C" void Buffer_Draw_Stamp(void const *thisptr, void const *icondata, int icon, int x_pixel, int y_pixel, void const *remap)
{
	if (!thisptr || !icondata || icon < 0) {
		return;
	}
	GraphicViewPortClass *vp = (GraphicViewPortClass *)thisptr;
	if (!vp->Get_Graphic_Buffer()) {
		return;
	}
	const unsigned long stamp_identity_key = ST_SPRITE_CACHE_Frame_Identity_Key(icondata, icon);
	if (!remap && AllowHardwareBlitFills && VP_Is_Planar(vp) && Ensure_Terrain_Tile_Scratch()) {
		ST_Tile_Cache_Debug_Toggle_Maybe();
		BOOL maybe_24x24_tile = FALSE;
		const unsigned char *base = (const unsigned char *)icondata;
		const unsigned short iw = Read_LE16_Unsafe(base + 0);
		const unsigned short ih = Read_LE16_Unsafe(base + 2);
		const unsigned short icount = Read_LE16_Unsafe(base + 4);
		const unsigned long icons_off = Read_LE32_Unsafe(base + 12);
		if (iw == ST_TILE_LINEAR_W && ih == ST_TILE_LINEAR_H && icount > 0 && icons_off > 0) {
			maybe_24x24_tile = TRUE;
		} else {
			const int tdw = Get_TD_SHP_Width(icondata);
			const int tdh = Get_TD_SHP_Height(icondata);
			if (tdw == ST_TILE_LINEAR_W && tdh == ST_TILE_LINEAR_H) {
				maybe_24x24_tile = TRUE;
			}
		}
		if (maybe_24x24_tile && Try_Blit_Cached_Terrain_Tile(vp, stamp_identity_key, x_pixel, y_pixel)) {
			return;
		}
	}
	if (!_ShapeBuffer || _ShapeBufferSize <= 0) {
		return;
	}

	void *decoded_ptr = NULL;
	int w = 0;
	int h = 0;
	/*
	 * Most shape draws treat index 0 as transparent, but terrain/iconset tiles use
	 * full 8bpp data where 0 is a valid color. Start with transparent semantics and
	 * disable it for iconset-decoded tiles.
	 */
	BOOL use_shape_transparency = TRUE;

	/*
	 * First try iconset-layout stamps (legacy ICN loaded blocks).
	 * Header fields are little-endian offsets; decode with byte reads.
	 */
	{
		const unsigned char *base = (const unsigned char *)icondata;
		const unsigned short iw = Read_LE16_Unsafe(base + 0);
		const unsigned short ih = Read_LE16_Unsafe(base + 2);
		const unsigned short icount = Read_LE16_Unsafe(base + 4);
		const unsigned long total_size = Read_LE32_Unsafe(base + 8);
		const unsigned long icons_off = Read_LE32_Unsafe(base + 12);
		const unsigned long map_off = Read_LE32_Unsafe(base + 28);
		if (iw > 0 && ih > 0 && iw <= 128 && ih <= 128 && icount > 0 && icons_off > 0) {
			const long logical_count = (long)iw * (long)ih;
			int icon_index = icon;
			if (map_off > 0) {
				if (icon < 0 || icon >= logical_count) {
					goto iconset_decode_done;
				}
				const unsigned char *map_ptr = base + map_off;
				icon_index = (int)map_ptr[icon];
			} else {
				if (icon < 0 || icon >= (int)icount) {
					goto iconset_decode_done;
				}
			}
			if (icon_index >= 0 && icon_index < (int)icount) {
				const long icon_size = (long)iw * (long)ih;
				const unsigned char *icon_ptr = base + icons_off + (long)icon_index * icon_size;
				const unsigned long icon_end = icons_off + (unsigned long)((long)icon_index * icon_size) + (unsigned long)icon_size;
				if (icon_size > 0 && icon_size <= _ShapeBufferSize
					&& (total_size == 0 || icon_end <= total_size)) {
					Mem_Copy(icon_ptr, _ShapeBuffer, icon_size);
					decoded_ptr = _ShapeBuffer;
					w = (int)iw;
					h = (int)ih;
					use_shape_transparency = FALSE;
				}
			}
		}
iconset_decode_done:
		;
	}

	/*
	 * Try TD SHP block decode first (templ/terrain icon sets are typically this format).
	 * Decode output is linear 8bpp frame bytes in _ShapeBuffer.
	 */
	if (!decoded_ptr) {
		w = Get_TD_SHP_Width(icondata);
		h = Get_TD_SHP_Height(icondata);
		if (w > 0 && h > 0 && (long)(w * h) <= _ShapeBufferSize) {
			int td_decoded = Decode_TD_SHP_Frame(icondata, icon, _ShapeBuffer, (int)_ShapeBufferSize);
			if (td_decoded > 0) {
				decoded_ptr = _ShapeBuffer;
			}
		}
	}

	/*
	 * Fallback: classic SHP shape block (extract frame then decode shape stream).
	 */
	if (!decoded_ptr) {
		void *shape = Extract_Shape(icondata, icon);
		if (shape) {
			w = Get_Shape_Width(shape);
			h = Get_Shape_Height(shape);
			if (w > 0 && h > 0 && (long)(w * h) <= _ShapeBufferSize) {
				int decoded = Decode_Shape_To_Buffer(shape, _ShapeBuffer, (int)_ShapeBufferSize);
				if (decoded > 0) {
					decoded_ptr = _ShapeBuffer;
				}
			}
		}
	}

	/* Last resort: KeyFrame decode path. */
	if (!decoded_ptr) {
		unsigned long frame_ptr = Build_Frame(icondata, (unsigned short)icon, _ShapeBuffer);
		if (frame_ptr) {
			w = (int)Get_Build_Frame_Width(icondata);
			h = (int)Get_Build_Frame_Height(icondata);
			decoded_ptr = (void *)frame_ptr;
		}
	}

	if (!decoded_ptr) {
		return;
	}

	if (w <= 0 || h <= 0) {
		return;
	}
	/*
	 * Fast terrain-tile path for ST planar targets:
	 * decode -> 24x24 linear scratch -> 64x24 planar scratch -> blit to destination.
	 * Viewport clipping matches Buffer_Frame_To_Page (WINSTUB.CPP): C2P + blit only the
	 * visible sub-rectangle so partially covered edge tiles stay on this path.
	 */
	if (w == ST_TILE_LINEAR_W && h == ST_TILE_LINEAR_H
		&& !remap
		&& AllowHardwareBlitFills
		&& VP_Is_Planar(vp)
		&& Ensure_Terrain_Tile_Scratch()) {
		const int vpw = vp->Get_Width();
		const int vph = vp->Get_Height();
		int dst_x = x_pixel;
		int dst_y = y_pixel;
		int clip_src_x = 0;
		int clip_src_y = 0;
		int clip_blit_w = ST_TILE_LINEAR_W;
		int clip_blit_h = ST_TILE_LINEAR_H;
		if (dst_x < 0) {
			clip_src_x = -dst_x;
			clip_blit_w -= clip_src_x;
			dst_x = 0;
		}
		if (dst_y < 0) {
			clip_src_y = -dst_y;
			clip_blit_h -= clip_src_y;
			dst_y = 0;
		}
		if (dst_x + clip_blit_w > vpw) {
			clip_blit_w = vpw - dst_x;
		}
		if (dst_y + clip_blit_h > vph) {
			clip_blit_h = vph - dst_y;
		}

		if (clip_blit_w > 0 && clip_blit_h > 0) {
			GraphicBufferClass *dst_gb = vp->Get_Graphic_Buffer();
			uint8_t *dst_root = (dst_gb && GB_Uses_ST_Planar_Surface(dst_gb))
				? (uint8_t *)dst_gb->Get_Buffer() : NULL;
			const int dx_abs = vp->Get_XPos() + dst_x;
			const int dy_abs = vp->Get_YPos() + dst_y;
			const int dst_bpl_fb = dst_gb ? GB_ST_Planar_Row_Bytes(dst_gb) : 0;
			const int dst_pw_fb = dst_gb ? dst_gb->Get_Width() : 0;
			const int dst_ph_fb = dst_gb ? dst_gb->Get_Height() : 0;
			if (dst_root && dst_gb
				&& dx_abs >= 0 && dy_abs >= 0
				&& dx_abs + clip_blit_w <= dst_pw_fb
				&& dy_abs + clip_blit_h <= dst_ph_fb) {
				STTilePlanarCacheEntry *cache_entry =
					Find_Tile_Planar_Cache_Entry(stamp_identity_key);
				if (cache_entry) {
					if (ST_Blitter_Planar_Rect_Blit(
							g_tile_planar_cache_aligned,
							ST_TILE_PLANAR_CACHE_BPL,
							ST_TILE_PLANAR_CACHE_W,
							ST_TILE_PLANAR_CACHE_H,
							(int)cache_entry->atlas_x + clip_src_x,
							(int)cache_entry->atlas_y + clip_src_y,
							dst_root,
							dst_bpl_fb,
							dst_pw_fb,
							dst_ph_fb,
							dx_abs,
							dy_abs,
							clip_blit_w,
							clip_blit_h)) {
						return;
					}
				}
				memcpy(g_tile_linear_24x24, decoded_ptr, (size_t)ST_TILE_LINEAR_BYTES);
				/*
				 * If index 0 must be transparent, the blitter D=S path is not correct.
				 * Check only the sub-rect we would draw (matches viewport clip).
				 */
				if (use_shape_transparency) {
					for (int ry = 0; ry < clip_blit_h; ry++) {
						uint8_t *row = g_tile_linear_24x24
							+ (size_t)(clip_src_y + ry) * ST_TILE_LINEAR_W + clip_src_x;
						if (memchr(row, 0, (size_t)clip_blit_w) != NULL) {
							goto fast24_fallback;
						}
					}
				}

				/* Cache the full 24x24 tile; clipping happens in blitter source coordinates. */
				cache_entry = Reserve_Tile_Planar_Cache_Entry();
				C2P_Render_Logical_To_Planar_Rect(
					g_tile_linear_24x24,
					ST_TILE_LINEAR_W,
					ST_TILE_LINEAR_H,
					ST_TILE_LINEAR_W,
					g_tile_planar_cache_aligned,
					ST_TILE_PLANAR_CACHE_BPL,
					ST_TILE_PLANAR_CACHE_W,
					ST_TILE_PLANAR_CACHE_H,
					(int)cache_entry->atlas_x,
					(int)cache_entry->atlas_y,
					0,
					0);
				if (ST_Blitter_Planar_Rect_Blit(
						g_tile_planar_cache_aligned,
						ST_TILE_PLANAR_CACHE_BPL,
						ST_TILE_PLANAR_CACHE_W,
						ST_TILE_PLANAR_CACHE_H,
						(int)cache_entry->atlas_x + clip_src_x,
						(int)cache_entry->atlas_y + clip_src_y,
						dst_root,
						dst_bpl_fb,
						dst_pw_fb,
						dst_ph_fb,
						dx_abs,
						dy_abs,
						clip_blit_w,
						clip_blit_h)) {
					cache_entry->identity_key = stamp_identity_key;
					cache_entry->last_used_tick = Next_Tile_Planar_Cache_Tick();
					cache_entry->valid = TRUE;
					return;
				}
			}
		} else {
			/* Tile fully outside viewport; nothing to draw. */
			return;
		}
	}

fast24_fallback:
	/*
	 * Planar LRU keys use the logical tile identity; clip is handled by blitter source offsets.
	 */
	if (w == ST_TILE_LINEAR_W && h == ST_TILE_LINEAR_H
		&& !remap
		&& VP_Is_Planar(vp)) {
		static unsigned long s_last_warn_hz200 = 0;
		unsigned long now_hz200 = ST_Read_Hz200();
		/* Limit warning spam: at most one warning per second. */
		if (now_hz200 - s_last_warn_hz200 >= 200UL) {
			printf("WARNING: terrain tile fell back from blitter fast path (icon=%d)\n", icon);
			s_last_warn_hz200 = now_hz200;
		}
	}
	{
		Bftp_ExArgs stamp_ex = { 0 };
		stamp_ex.identity_key = stamp_identity_key;
		Buffer_Frame_To_Page_Ex(
			x_pixel,
			y_pixel,
			w,
			h,
			decoded_ptr,
			*vp,
			SHAPE_WIN_REL | (use_shape_transparency ? ST_SHAPE_TRANS_FLAG : 0),
			&stamp_ex);
	}
}

/*=========================================================================*/
/* Buffer_Draw_Stamp_Clip -- Draws a stamp/icon with clipping              */
/*=========================================================================*/
extern "C" void Buffer_Draw_Stamp_Clip(void const *thisptr, void const *icondata, int icon, int x_pixel, int y_pixel, void const *remap, int, int, int, int)
{
	/* Current callers pass viewport bounds; Buffer_Frame_To_Page clips to viewport. */
	Buffer_Draw_Stamp(thisptr, icondata, icon, x_pixel, y_pixel, remap);
}

