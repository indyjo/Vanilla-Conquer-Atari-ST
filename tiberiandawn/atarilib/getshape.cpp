/*
 * getshape.cpp - Shape extraction functions for Atari ST/MiNT
 * 
 * Handles endianness conversion for shape files (stored in little-endian format).
 */

#include "shape.h"
#include "iff.h"   // LCW_Uncompress for Decode_Shape_To_Buffer
#include <stddef.h>  // For NULL
#include <string.h>  // For memcpy, memset

// Helper to read little-endian 32-bit values (byte-by-byte for m68k alignment)
static inline unsigned long ReadLE32(const unsigned char *bytes)
{
	unsigned long a = (unsigned long)(unsigned char)bytes[0];
	unsigned long b = (unsigned long)(unsigned char)bytes[1];
	unsigned long c = (unsigned long)(unsigned char)bytes[2];
	unsigned long d = (unsigned long)(unsigned char)bytes[3];
	return a | (b << 8) | (c << 16) | (d << 24);
}

// Helper to read little-endian 16-bit values (byte-by-byte for m68k alignment safety)
static inline unsigned short ReadLE16(const unsigned char *bytes)
{
	unsigned int lo = (unsigned int)(unsigned char)bytes[0];
	unsigned int hi = (unsigned int)(unsigned char)bytes[1];
	return (unsigned short)(lo | (hi << 8));
}

/***************************************************************************
 * Extract_Shape_Count -- returns # of shapes in the given shape block   *
 *                                                                         *
 * INPUT:                                                                  *
 * buffer	pointer to shape block                                          *
 *                                                                         *
 * OUTPUT:                                                                 *
 * # shapes in the block                                                   *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/GETSHAPE.CPP with endianness handling           *
 *=========================================================================*/
int Extract_Shape_Count(void const *buffer)
{
	if (!buffer) return 0;
	
	// NumShapes is the first 16-bit value, stored as little-endian
	const unsigned char *bytes = (const unsigned char*)buffer;
	return (int)ReadLE16(bytes);
}

/***************************************************************************
 * Extract_Shape -- Gets pointer to shape in given shape block            *
 *                                                                         *
 * INPUT:                                                                  *
 * buffer	pointer to shape block                                          *
 * shape	index of shape to get                                            *
 *                                                                         *
 * OUTPUT:                                                                 *
 * pointer to shape in the shape block                                     *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/GETSHAPE.CPP with endianness handling           *
 *=========================================================================*/
void *Extract_Shape(void const *buffer, int shape)
{
	if (!buffer || shape < 0) {
		return NULL;
	}
	
	const unsigned char *bytes = (const unsigned char*)buffer;
	
	// Read NumShapes (first 16-bit value, little-endian)
	unsigned short num_shapes = ReadLE16(bytes);
	
	if (shape >= num_shapes) {
		return NULL;
	}
	
	// Offsets array starts at offset 2 (after NumShapes)
	// Each offset is a 32-bit little-endian value
	// Read the offset for the requested shape
	unsigned long offset = ReadLE32(bytes + 2 + (shape * 4));
	
	// Return pointer to shape data (offset is from start of block, but skip NumShapes)
	return (void*)(bytes + 2 + offset);
}

/***************************************************************************
 * Get_Shape_Width -- gets shape width in pixels                           *
 *                                                                         *
 * INPUT:                                                                  *
 * shape	pointer to a shape                                               *
 *                                                                         *
 * OUTPUT:                                                                 *
 * shape width in pixels                                                   *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/GETSHAPE.CPP with endianness handling           *
 *=========================================================================*/
int Get_Shape_Width(void const *shape)
{
	if (!shape) return 0;
	
	const unsigned char *data = (const unsigned char*)shape;
	
	// Shape_Type structure (little-endian):
	// unsigned short ShapeType (offset 0-1)
	// unsigned char Height (offset 2)
	// unsigned short Width (offset 3-4 or 4-5 depending on alignment)
	// Try both packed and aligned layouts
	unsigned short width_packed = ReadLE16(data + 3);
	unsigned short width_aligned = ReadLE16(data + 4);
	
	// Width is in bytes, not pixels (for 8-bit pixels, bytes = pixels)
	// Use the value that's reasonable (typically 1-64 bytes for cursors)
	if (width_packed > 0 && width_packed <= 64) {
		return (int)width_packed;
	} else if (width_aligned > 0 && width_aligned <= 64) {
		return (int)width_aligned;
	}
	
	// Default to aligned if neither looks right
	return (int)width_aligned;
}

/***************************************************************************
 * Get_Shape_Height -- gets shape height in pixels                         *
 *                                                                         *
 * INPUT:                                                                  *
 * shape	pointer to a shape                                               *
 *                                                                         *
 * OUTPUT:                                                                 *
 * shape height in pixels                                                  *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/GETSHAPE.CPP with endianness handling           *
 *=========================================================================*/
int Get_Shape_Height(void const *shape)
{
	if (!shape) return 0;
	
	const unsigned char *data = (const unsigned char*)shape;
	
	// Height is at offset 2 (unsigned char)
	return (int)data[2];
}

/***************************************************************************
 * Get_Shape_Uncomp_Size -- gets shape's uncompressed size in bytes       *
 *                                                                         *
 * INPUT:                                                                  *
 * shape	pointer to shape                                                 *
 *                                                                         *
 * OUTPUT:                                                                 *
 * shape's size in bytes when uncompressed                                 *
 *                                                                         *
 * HISTORY:                                                                *
 *   Ported from WIN32LIB/GETSHAPE.CPP with endianness handling           *
 *=========================================================================*/
int Get_Shape_Uncomp_Size(void const *shape)
{
	if (!shape) return 0;
	
	const unsigned char *data = (const unsigned char*)shape;
	
	// DataLength is at offset 8-9 or 10-11 depending on alignment (little-endian)
	unsigned short len_packed = ReadLE16(data + 8);
	unsigned short len_aligned = ReadLE16(data + 10);
	
	// Use the value that's reasonable
	if (len_packed > 0 && len_packed < 65535) {
		return (int)len_packed;
	} else if (len_aligned > 0 && len_aligned < 65535) {
		return (int)len_aligned;
	}
	
	// Default to aligned
	return (int)len_aligned;
}

/***************************************************************************
 * Decode_Shape_To_Buffer -- Decode shape pixel data into a linear buffer  *
 *                                                                         *
 * INPUT:                                                                  *
 * shape   pointer to shape (Shape_Type + data)                            *
 * buffer  output buffer (width*height bytes, row-major)                   *
 * buf_size size of buffer in bytes                                        *
 *                                                                         *
 * OUTPUT:                                                                 *
 * width*height if decoded successfully, else 0                           *
 *                                                                         *
 * ShapeType bits:
 *   - MAKESHAPE_NOCOMP indicates the bytes after the header are already a
 *     NOCOMP RLE stream (length DataLength).
 *   - Otherwise, those bytes are LCW-compressed and the LCW output is the
 *     NOCOMP RLE stream (length DataLength).
 *   - MAKESHAPE_COMPACT enables 16-byte colortable remap for nonzero pixels.
 *=========================================================================*/
int Decode_Shape_To_Buffer(void const *shape, void *buffer, int buf_size)
{
	/*
	** Decode to match WIN32LIB/SRCDEBUG/WWMOUSE.ASM:
	**
	**  - For MAKESHAPE_NOCOMP shapes:
	**      bytes after the header are already a NOCOMP "RLE stream"
	**      of length DataLength (not raw pixels).
	**  - Otherwise:
	**      bytes after the header are an LCW stream.
	**      LCW output is a NOCOMP "RLE stream" of length DataLength.
	**  - Then expand that NOCOMP RLE stream into final WxH pixels:
	**      nonzero byte => literal pixel value
	**      zero byte      => next byte is count of transparent zeros
	**  - For MAKESHAPE_COMPACT:
	**      nonzero stream bytes are indices into 16-byte colortable.
	**
	** Output pixels are stored as bytes with 0 meaning transparent.
	*/
	if (!shape || !buffer || buf_size <= 0) return 0;

	const unsigned char *data = (const unsigned char *)shape;
	const unsigned short shape_type = ReadLE16(data + 0);
	const bool compact = (shape_type & 0x0001) != 0;   /* MAKESHAPE_COMPACT  */
	const bool nocomp  = (shape_type & 0x0002) != 0;   /* MAKESHAPE_NOCOMP   */
	const int height = (int)(unsigned char)data[2];

	if (height <= 0 || height > 64) return 0;

	/* Decode helpers */
	auto decode_nocomp_rle_to_pixels = [&](const unsigned char *stream, int stream_len, unsigned char *out, int out_len) -> int {
		int in_i = 0;
		int out_i = 0;
		while (out_i < out_len) {
			if (in_i >= stream_len) return 0;
			unsigned char b = stream[in_i++];
			if (b == 0) {
				if (in_i >= stream_len) return 0;
				unsigned char cnt = stream[in_i++];
				if (out_i + (int)cnt > out_len) return 0;
				memset(out + out_i, 0, (size_t)cnt);
				out_i += (int)cnt;
			} else {
				out[out_i++] = b;
			}
		}
		return (out_i == out_len) ? out_len : 0;
	};

	auto decode_compact_nocomp_rle_to_pixels = [&](const unsigned char *stream, int stream_len,
	                                                 const unsigned char *remap16, unsigned char *out, int out_len) -> int {
		int in_i = 0;
		int out_i = 0;
		while (out_i < out_len) {
			if (in_i >= stream_len) return 0;
			unsigned char b = stream[in_i++];
			if (b == 0) {
				if (in_i >= stream_len) return 0;
				unsigned char cnt = stream[in_i++];
				if (out_i + (int)cnt > out_len) return 0;
				memset(out + out_i, 0, (size_t)cnt);
				out_i += (int)cnt;
			} else {
				if (b >= 16) return 0;
				out[out_i++] = remap16[b];
			}
		}
		return (out_i == out_len) ? out_len : 0;
	};

	auto try_layout = [&](int width_off, int datalen_off, int header_noncompact, int header_compact, int remap_off) -> int {
		int width = (int)ReadLE16(data + width_off);
		if (width <= 0 || width > 64) return 0;
		int need = width * height;
		if (need <= 0 || need > buf_size) return 0;

		const unsigned short data_length = ReadLE16(data + datalen_off);
		if (data_length == 0) return 0;

		const int header_size = compact ? header_compact : header_noncompact;
		const unsigned char *payload = data + header_size;
		unsigned char *tmp = NULL;
		const unsigned char *stream = NULL;
		int stream_len = (int)data_length;

		/* Acquire NOCOMP RLE stream bytes */
		if (nocomp) {
			/* Already NOCOMP stream bytes after header */
			stream = payload;
		} else {
			/*
			** Compressed: LCW stream output is NOCOMP RLE stream of length DataLength.
			** Decompress into temporary buffer.
			*/
			/*
			** Prefer the global _ShapeBuffer if big enough.
			** (It is already allocated during init.)
			*/
			if (_ShapeBuffer && _ShapeBufferSize >= (long)stream_len) {
				tmp = (unsigned char *)_ShapeBuffer;
			} else {
				tmp = new unsigned char[stream_len];
				if (!tmp) return 0;
			}

			memset(tmp, 0, (size_t)stream_len);

			unsigned long out_len = LCW_Uncompress((void *)payload, tmp, (unsigned long)stream_len);
			bool ok = (out_len == (unsigned long)stream_len);

			if (!ok) {
				/*
				** Optional CompHeaderType wrapper:
				** Only try when the header is sane to avoid writing past tmp.
				*/
				unsigned char method = payload[0];
				unsigned long uncomp_size = ReadLE32(payload + 2);
				unsigned short skip = ReadLE16(payload + 6);
				if (method <= 4 && uncomp_size == (unsigned long)stream_len && skip <= 512) {
					memset(tmp, 0, (size_t)stream_len);
					unsigned long out2 = Uncompress_Data(payload, tmp);
					ok = (out2 == (unsigned long)stream_len);
				}
			}

			if (!ok) {
				if (tmp && tmp != (unsigned char *)_ShapeBuffer) delete[] tmp;
				return 0;
			}
			stream = tmp;
		}

		/* Expand NOCOMP stream into final WxH pixels */
		unsigned char *out = (unsigned char *)buffer;
		int decoded = 0;
		if (compact) {
			const unsigned char *remap16 = data + remap_off;
			decoded = decode_compact_nocomp_rle_to_pixels(stream, stream_len, remap16, out, need);
		} else {
			decoded = decode_nocomp_rle_to_pixels(stream, stream_len, out, need);
		}

		if (nocomp == false && tmp && tmp != (unsigned char *)_ShapeBuffer) {
			delete[] tmp;
		}
		return decoded;
	};

	/* Try packed/ASM layout first: width at +3, DataLength at +8, header sizes 10/26. */
	int dec1 = try_layout(/*width_off*/ 3, /*datalen_off*/ 8,
	                      /*header_noncompact*/ 10, /*header_compact*/ 26,
	                      /*remap_off*/ 10);
	if (dec1) return dec1;

	/* Fallback aligned layout: width at +4, DataLength at +10, header sizes 12/28. */
	int dec2 = try_layout(/*width_off*/ 4, /*datalen_off*/ 10,
	                      /*header_noncompact*/ 12, /*header_compact*/ 28,
	                      /*remap_off*/ 12);
	if (dec2) return dec2;

	return 0;
}

/*---------------------------------------------------------------------------
 * Tiberian Dawn SHP format (14-byte header + frame table, LCW per frame).
 * Used by MOUSE.SHP and other C&C assets from mix files.
 *---------------------------------------------------------------------------*/

/* TD SHP header offsets. File format is little-endian; read byte-by-byte for m68k. */
#define TD_SHP_FRAMES   0
#define TD_SHP_WIDTH    6
#define TD_SHP_HEIGHT   8
#define TD_SHP_HEADER   14
#define TD_SHP_ENTRY    8

/* Read LE16 at base[off] without assuming alignment */
static inline unsigned short ReadLE16_at(const unsigned char *base, int off) {
	unsigned int lo = (unsigned int)(unsigned char)base[off];
	unsigned int hi = (unsigned int)(unsigned char)base[off + 1];
	return (unsigned short)(lo | (hi << 8));
}
/* Read LE 24-bit at base[off] */
static inline unsigned long ReadLE24_at(const unsigned char *base, int off) {
	unsigned long a = (unsigned long)(unsigned char)base[off];
	unsigned long b = (unsigned long)(unsigned char)base[off + 1];
	unsigned long c = (unsigned long)(unsigned char)base[off + 2];
	return a | (b << 8) | (c << 16);
}

int Get_TD_SHP_Width(void const *block)
{
	if (!block) return 0;
	return (int)ReadLE16_at((const unsigned char *)block, TD_SHP_WIDTH);
}

int Get_TD_SHP_Height(void const *block)
{
	if (!block) return 0;
	return (int)ReadLE16_at((const unsigned char *)block, TD_SHP_HEIGHT);
}

/***************************************************************************
 * Decode_TD_SHP_Frame -- Decode one frame from a Tiberian Dawn SHP block  *
 *                                                                         *
 * INPUT:  block = full SHP block, frame_index = frame number (0..Frames-1)*
 *         buffer = output (width*height bytes), buf_size = buffer size    *
 * OUTPUT: width*height if decoded, else 0                                 *
 *         Only DataFormat 0x80 (LCW) is supported.                        *
 *=========================================================================*/
int Decode_TD_SHP_Frame(void const *block, int frame_index, void *buffer, int buf_size)
{
	if (!block || !buffer || buf_size <= 0 || frame_index < 0) return 0;
	const unsigned char *b = (const unsigned char *)block;
	/* All multi-byte values are little-endian in file; read byte-by-byte for big-endian hosts (m68k) */
	unsigned short frames = ReadLE16_at(b, TD_SHP_FRAMES);
	int w = (int)ReadLE16_at(b, TD_SHP_WIDTH);
	int h = (int)ReadLE16_at(b, TD_SHP_HEIGHT);

	if (frames == 0 || frames > 256 || frame_index >= frames) return 0;
	if (w <= 0 || h <= 0 || w > 64 || h > 64) return 0; /* sane cursor size */
	int need = w * h;
	if (need > buf_size) return 0;
	/* Frame table: 14 + frame_index*8. Each entry: DataOffset (24-bit LE), DataFormat (1 byte), ... */
	int entry = TD_SHP_HEADER + frame_index * TD_SHP_ENTRY;
	unsigned long data_offset = ReadLE24_at(b, entry);
	unsigned char data_format = b[entry + 3];

	if (data_format != 0x80) /* LCW */
		return 0;
	/* DataOffset is from start of file (ModdingWiki) */
	const unsigned char *payload = b + data_offset;
	memset(buffer, 0, (unsigned)need);
	unsigned long out_len = LCW_Uncompress((void *)payload, buffer, (unsigned long)need);
	if (out_len == (unsigned long)need) {
		return need;
	}
	/* Fallback: some SHP variants use offset relative to frame table */
	payload = b + TD_SHP_HEADER + data_offset;
	memset(buffer, 0, (unsigned)need);
	out_len = LCW_Uncompress((void *)payload, buffer, (unsigned long)need);
	return (out_len == (unsigned long)need) ? need : 0;
}
