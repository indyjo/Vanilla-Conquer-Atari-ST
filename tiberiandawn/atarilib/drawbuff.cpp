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
#include "st_blit.h"
#include "st_sprite_cache.h"
#include "st_planar_draw.h"
#include "st16_draw.h"
#include "st16_iconset.h"
#include "st16_convert.h"
#include "memflag.h"
#include "tile.h"
#include "endianness.h"
#include <stdio.h>
#include <string.h>  // For memset

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
/*
 * Planar font fast path: per-plane mask/color at bit 0 only (0xFFFF = preserve pixel).
 * Rebuilt from ColorXlat[0..15] via C2P_MapNearestLUT (solid pens, no Bayer).
 */
static uint16_t FontPlanarMask[4][16];
static uint16_t FontPlanarColor[4][16];

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
	** ST planar self-blit fast path (HW or SW via ST_Blit).
	*/
	if (src_planar && dst_planar && !trans
		&& src_gb == dest_gb
		&& src_root && dst_root) {
		const int sx_abs = src_vp->Get_XPos() + x_pixel;
		const int sy_abs = src_vp->Get_YPos() + y_pixel;
		const int dx_abs = dest_vp->Get_XPos() + dx_pixel;
		const int dy_abs = dest_vp->Get_YPos() + dy_pixel;
		const int src_bpl = GB_ST_Planar_Row_Bytes(src_gb);
		const int dst_bpl = GB_ST_Planar_Row_Bytes(dest_gb);
		if (ST_Blit_Planar_Rect_Blit(
				src_root,
				src_bpl,
				sx_abs,
				sy_abs,
				dst_root,
				dst_bpl,
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
/* Planar font tables and row rasterizer (no per-pixel PutPixel).           */
/*=========================================================================*/

static void Font_Planar_Fill_Slot(int font_idx, unsigned char pal_idx)
{
	if (font_idx < 0 || font_idx > 15)
		return;

	if (pal_idx == 0) {
		for (int pl = 0; pl < 4; pl++) {
			FontPlanarMask[pl][font_idx] = 0xFFFF;
			FontPlanarColor[pl][font_idx] = 0;
		}
		return;
	}

	const unsigned char pen = C2P_Map8ToNearest4(pal_idx);
	for (int pl = 0; pl < 4; pl++) {
		FontPlanarMask[pl][font_idx] = 0xFFFE;
		FontPlanarColor[pl][font_idx] = (pen & (unsigned char)(1u << pl)) ? (uint16_t)1 : (uint16_t)0;
	}
}

void Font_Planar_Rebuild_Tables(void)
{
	for (int f = 0; f < 16; f++)
		Font_Planar_Fill_Slot(f, ColorXlat[f]);
}

static inline void Font_Planar_Rebuild_Slot(int font_idx)
{
	Font_Planar_Fill_Slot(font_idx, ColorXlat[font_idx]);
}

static inline uint16_t Font_Planar_Rol_Left16(uint16_t word, int count)
{
	count &= 15;
	if (count == 0)
		return word;
	return (uint16_t)((word << count) | (word >> (16 - count)));
}

static inline uint16_t Font_Planar_Rol_Right16(uint16_t word, int count)
{
	count &= 15;
	if (count == 0)
		return word;
	return (uint16_t)((word >> count) | (word << (16 - count)));
}

static inline uint16_t Font_Planar_Apply_Lsb(uint16_t word, uint16_t mask, uint16_t color)
{
	return (uint16_t)((word & mask) | color);
}

static inline unsigned char Font_Planar_Next_Font_Idx(const unsigned char **font_data, int *low_nibble_next)
{
	if (*low_nibble_next) {
		const unsigned char f = (unsigned char)((**font_data >> 4) & 0x0F);
		(*font_data)++;
		*low_nibble_next = 0;
		return f;
	}
	const unsigned char f = (unsigned char)(**font_data & 0x0F);
	*low_nibble_next = 1;
	return f;
}

/*
 * Draw one scanline on interleaved ST planar memory.
 * font_data: packed font bytes (low nibble first); NULL = solid_slot for every pixel.
 * Screen bit 15-(x&15) is brought to the LSB with ror.w; rol.w #1 steps to the next column.
 */
static void Font_Planar_Draw_Row(
	uint8_t *planar_root,
	int row_bytes,
	int abs_x0,
	int abs_y,
	unsigned char width,
	const unsigned char *font_data,
	unsigned char solid_slot)
{
	if (!planar_root || row_bytes <= 0 || width == 0)
		return;

	uint8_t *row = planar_root + (size_t)abs_y * (size_t)row_bytes;

	for (int pl = 0; pl < 4; pl++) {
		unsigned char col = 0;
		int abs_x = abs_x0;
		const unsigned char *fp = font_data;
		int low_nibble_next = 0;

		while (col < width) {
			const int widx = abs_x >> 4;
			const int align = 15 - (abs_x & 15);
			int run = align + 1;
			if ((int)(width - col) < run)
				run = (int)width - (int)col;

			uint16_t *wp = (uint16_t *)(void *)(row + widx * 8) + pl;
			uint16_t w = *wp;
			if (align)
				w = Font_Planar_Rol_Right16(w, align);

			for (int i = 0; i < run; i++) {
				unsigned char f;
				if (font_data) {
					f = Font_Planar_Next_Font_Idx(&fp, &low_nibble_next);
				} else {
					f = solid_slot;
				}
				w = Font_Planar_Apply_Lsb(w, FontPlanarMask[pl][f], FontPlanarColor[pl][f]);
				if (i + 1 < run)
					w = Font_Planar_Rol_Left16(w, 1);
			}

			{
				const int step = run - 1;
				if (step)
					w = Font_Planar_Rol_Right16(w, step);
				if (align)
					w = Font_Planar_Rol_Left16(w, align);
				*wp = w;
			}

			col = (unsigned char)(col + (unsigned char)run);
			abs_x += run;
		}
	}
}

static void Font_Planar_Draw_Glyph_Data_Row(
	int abs_x0,
	int abs_y,
	uint8_t *planar_root,
	int row_bytes,
	unsigned char charwidth,
	const unsigned char *data_ptr)
{
	if (charwidth > 64)
		return;

	Font_Planar_Draw_Row(planar_root, row_bytes, abs_x0, abs_y, charwidth, data_ptr, 0);
}

static void Font_Planar_Draw_Solid_Row(
	int abs_x0,
	int abs_y,
	uint8_t *planar_root,
	int row_bytes,
	unsigned char charwidth)
{
	Font_Planar_Draw_Row(planar_root, row_bytes, abs_x0, abs_y, charwidth, NULL, 0);
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
	
	const int planar_fast = VP_Is_Planar(vp);

	// Set up color translation table
	ColorXlat[0] = (unsigned char)bcolor;
	ColorXlat[1] = (unsigned char)fcolor;
	ColorXlat[16] = (unsigned char)fcolor;
	if (planar_fast) {
		/* Buffer_Print only patches slots 0/1; gradient slots come from Set_Font_Palette. */
		Font_Planar_Rebuild_Slot(0);
		Font_Planar_Rebuild_Slot(1);
	}

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
	uint8_t *planar_root = NULL;
	int planar_row_bytes = 0;
	int abs_xpos = 0;
	int abs_ypos = 0;
	if (!planar_fast) {
		curline = viewport_base + cur_y * bufferwidth;
		startdraw = curline + cur_x;
	} else {
		GraphicBufferClass *gbp = vp->Get_Graphic_Buffer();
		planar_root = (uint8_t *)gbp->Get_Buffer();
		planar_row_bytes = GB_ST_Planar_Row_Bytes(gbp);
		abs_xpos = vp->Get_XPos();
		abs_ypos = vp->Get_YPos();
		startdraw = planar_root;
	}

	// Process each character
	const char *string = str;
	while (*string) {
		unsigned char ch = (unsigned char)*string++;
		
		// Handle line feed (LF = 10) or carriage return (CR = 13)
		if (ch == 10 || ch == 13) {
			cur_y += maxheight + FontYSpacing;
			if (cur_y + maxheight > (unsigned)vpheight) break;
			
			if (!planar_fast)
				curline = viewport_base + cur_y * bufferwidth;

			// CR returns to original x, LF goes to x=0
			if (ch == 13) {
				cur_x = original_x;
			} else {
				cur_x = 0;
			}

			if (!planar_fast)
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
			
			if (!planar_fast)
				curline = viewport_base + cur_y * bufferwidth;
			cur_x = original_x;
			if (!planar_fast)
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
		unsigned char draw_width = charwidth;
		if (cur_x >= (unsigned)vpwidth) {
			draw_width = 0;
		} else if (cur_x + (unsigned)charwidth > (unsigned)vpwidth) {
			draw_width = (unsigned char)((unsigned)vpwidth - cur_x);
		}
		const int abs_char_x = abs_xpos + cur_x;

		// Draw top blank area (skip when background palette index is 0)
		if (topblank > 0) {
			if (ColorXlat[0] != 0) {
				if (planar_fast && draw_width > 0) {
					for (unsigned char row = 0; row < topblank; row++) {
						if (cur_y + row < (unsigned)vpheight)
							Font_Planar_Draw_Solid_Row(
								abs_char_x, abs_ypos + cur_y + row, planar_root, planar_row_bytes, draw_width);
					}
				} else {
					unsigned char bgcolor = ColorXlat[0];
					for (unsigned char row = 0; row < topblank; row++) {
						for (unsigned char col = 0; col < charwidth; col++) {
							if (cur_x + col < (unsigned)vpwidth && cur_y + row < (unsigned)vpheight) {
								unsigned char *row_ptr = draw_ptr;
								row_ptr[col] = bgcolor;
							}
						}
						draw_ptr += bufferwidth;
					}
				}
			} else {
				if (!planar_fast)
					draw_ptr += topblank * bufferwidth;
			}
		}

		// Draw character data
		if (charheight > 0) {
			const unsigned char *data_ptr = chardata;
			for (unsigned char row = 0; row < charheight; row++) {
				if (cur_y + topblank + row >= (unsigned)vpheight)
					break;

				if (planar_fast && draw_width > 0 && charwidth <= 64) {
					Font_Planar_Draw_Glyph_Data_Row(
						abs_char_x,
						abs_ypos + cur_y + topblank + row,
						planar_root,
						planar_row_bytes,
						draw_width,
						data_ptr);
					const unsigned char bytes = (unsigned char)((charwidth + 1) / 2);
					data_ptr += bytes;
				} else if (planar_fast) {
					unsigned char col = 0;
					unsigned char remaining_width = charwidth;
					while (remaining_width > 0) {
						unsigned char data_byte = *data_ptr++;
						unsigned char pixel = data_byte & 0x0F;
						if (ColorXlat[pixel] != 0)
							Buffer_Put_Pixel(
								vp, cur_x + col, cur_y + topblank + row, ColorXlat[pixel]);
						col++;
						remaining_width--;
						if (remaining_width > 0) {
							pixel = (data_byte >> 4) & 0x0F;
							if (ColorXlat[pixel] != 0)
								Buffer_Put_Pixel(
									vp, cur_x + col, cur_y + topblank + row, ColorXlat[pixel]);
							col++;
							remaining_width--;
						}
					}
				} else {
					unsigned char col = 0;
					unsigned char remaining_width = charwidth;
					while (remaining_width > 0) {
						unsigned char data_byte = *data_ptr++;
						unsigned char pixel = data_byte & 0x0F;
						unsigned char color = ColorXlat[pixel];
						if (color != 0 && cur_x + col < (unsigned)vpwidth) {
							unsigned char *row_ptr = draw_ptr;
							row_ptr[col] = color;
						}
						col++;
						remaining_width--;
						if (remaining_width > 0) {
							pixel = (data_byte >> 4) & 0x0F;
							color = ColorXlat[pixel];
							if (color != 0 && cur_x + col < (unsigned)vpwidth) {
								unsigned char *row_ptr = draw_ptr;
								row_ptr[col] = color;
							}
							col++;
							remaining_width--;
						}
					}
					draw_ptr += bufferwidth;
				}
			}
		}

		// Draw bottom blank area
		if (bottomblank > 0 && ColorXlat[0] != 0) {
			if (planar_fast && draw_width > 0) {
				for (unsigned char row = 0; row < bottomblank; row++) {
					if (cur_y + topblank + charheight + row < (unsigned)vpheight)
						Font_Planar_Draw_Solid_Row(
							abs_char_x,
							abs_ypos + cur_y + topblank + charheight + row,
							planar_root,
							planar_row_bytes,
							draw_width);
				}
			} else {
				unsigned char bgcolor = ColorXlat[0];
				for (unsigned char row = 0; row < bottomblank; row++) {
					for (unsigned char col = 0; col < charwidth; col++) {
						if (cur_x + col < (unsigned)vpwidth
							&& cur_y + topblank + charheight + row < (unsigned)vpheight) {
							unsigned char *row_ptr = draw_ptr;
							row_ptr[col] = bgcolor;
						}
					}
					draw_ptr += bufferwidth;
				}
			}
		}
		
		// Update position for next character
		cur_x = next_x;
		if (!planar_fast) {
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

	if (x0 >= vpw || y0 >= vph || x1 < 0 || y1 < 0) {
		return;
	}

	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 >= vpw) x1 = vpw - 1;
	if (y1 >= vph) y1 = vph - 1;
	if (x0 > x1 || y0 > y1) {
		return;
	}

	const int rect_w = x1 - x0 + 1;
	const int rect_h = y1 - y0 + 1;

	if (VP_Is_Planar(vp)) {
		GraphicBufferClass *gb = vp->Get_Graphic_Buffer();
		uint8_t *root = (uint8_t *)gb->Get_Buffer();
		if (!root) {
			return;
		}
		C2P_Remap_Planar_Rect(
			root,
			GB_ST_Planar_Row_Bytes(gb),
			gb->Get_Width(),
			gb->Get_Height(),
			vp->Get_XPos() + x0,
			vp->Get_YPos() + y0,
			rect_w,
			rect_h,
			map);
		return;
	}

	unsigned char *viewport_base = (unsigned char *)vp->Get_Offset();
	if (!viewport_base) {
		return;
	}

	const int row_stride = Get_Row_Stride(vp);
	unsigned char *row = viewport_base + x0 + y0 * row_stride;
	for (int lines = rect_h; lines > 0; --lines) {
		for (int i = 0; i < rect_w; ++i) {
			row[i] = map[row[i]];
		}
		row += row_stride;
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

static void Buffer_Draw_Stamp_8bpp(
	GraphicViewPortClass *vp,
	void const *icondata,
	int icon,
	int x_pixel,
	int y_pixel,
	void const *remap)
{
	const IControl_Type *ic = (const IControl_Type *)icondata;
	int iw;
	int ih;
	int icount;
	unsigned long total_size;
	unsigned long icons_off;
	unsigned long map_off;
	int icon_index;
	long icon_size;
	const unsigned char *icon_ptr;
	unsigned long icon_end;

	if (!_ShapeBuffer || _ShapeBufferSize <= 0 || !ic) {
		return;
	}

	iw = (int)le16toh(ic->Width);
	ih = (int)le16toh(ic->Height);
	icount = (int)le16toh(ic->Count);
	total_size = (unsigned long)le32toh(ic->Size);
	icons_off = (unsigned long)le32toh(ic->Icons);
	map_off = (unsigned long)le32toh(ic->Map);

	if (iw <= 0 || ih <= 0 || iw > 128 || ih > 128 || icount <= 0 || icons_off <= 0) {
		return;
	}

	icon_index = icon;
	if (map_off > 0) {
		const unsigned char *const base = (const unsigned char *)icondata;
		if ((size_t)icon >= (size_t)icount) {
			return;
		}
		icon_index = (int)base[map_off + (size_t)icon];
	} else if (icon >= icount) {
		return;
	}

	if (icon_index < 0 || (uint8_t)icon_index == 0xFF) {
		return;
	}

	icon_size = (long)iw * (long)ih;
	if (icon_size <= 0 || icon_size > _ShapeBufferSize) {
		return;
	}

	icon_ptr = (const unsigned char *)icondata + icons_off + (long)icon_index * icon_size;
	icon_end = icons_off + (unsigned long)((long)icon_index * icon_size) + (unsigned long)icon_size;
	if (total_size != 0 && icon_end > total_size) {
		return;
	}

	Mem_Copy(icon_ptr, _ShapeBuffer, (unsigned long)icon_size);
	{
		Bftp_ExArgs stamp_ex = { 0 };
		stamp_ex.identity_root = icondata;
		stamp_ex.identity_frame = icon;
		Buffer_Frame_To_Page_Ex(
			x_pixel,
			y_pixel,
			iw,
			ih,
			_ShapeBuffer,
			*vp,
			SHAPE_WIN_REL,
			&stamp_ex);
	}
	(void)remap;
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

	if (remap || !VP_Is_Planar(vp)) {
		Buffer_Draw_Stamp_8bpp(vp, icondata, icon, x_pixel, y_pixel, remap);
		return;
	}

	IControl_Type *ic = (IControl_Type *)icondata;
	const uint8_t *base = (const uint8_t *)icondata;

	/* Fast path: native magic at 0x20 + native Icons == 0x2c.
	 * After ST16_Convert_InPlace all header fields are native-endian. */
	if (ST16_Has_Native_Chunk(ic)) {
		(void)ST16_Blit_Stamp(vp, ic, icon, x_pixel, y_pixel);
		return;
	}

	/* Pre-convert: LE byte reads only (header is still little-endian). */
	{
		const size_t blob_size = (size_t)ST16_Read_LE32(base + 8);

		if (!ST16_Iconset_Should_Convert(base, blob_size)) {
			Buffer_Draw_Stamp_8bpp(vp, icondata, icon, x_pixel, y_pixel, remap);
			return;
		}
	}
	if (!ST16_Iconset_Resolve(icondata, NULL)) {
		return;
	}
	(void)ST16_Blit_Stamp(vp, ic, icon, x_pixel, y_pixel);
}

/*=========================================================================*/
/* Buffer_Draw_Stamp_Clip -- Draws a stamp/icon with clipping              */
/*=========================================================================*/
extern "C" void Buffer_Draw_Stamp_Clip(void const *thisptr, void const *icondata, int icon, int x_pixel, int y_pixel, void const *remap, int, int, int, int)
{
	/* Current callers pass viewport bounds; Buffer_Frame_To_Page clips to viewport. */
	Buffer_Draw_Stamp(thisptr, icondata, icon, x_pixel, y_pixel, remap);
}

