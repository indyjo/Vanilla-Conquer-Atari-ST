/*
 * wsa.cpp - WSA (Westwood Animation) and XOR Delta functions for Atari ST/MiNT
 *
 * Port of core WSA animation logic from WIN32LIB/WSA.CPP.
 * Keeps behavior close to the original while using endian-safe reads for WSA data.
 */

#include "wsa.h"
#include "wwmem.h"
#include "WIN32LIB/FILE.H"
#include "misc.h"
#include "iff.h"
#include "drawbuff.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#ifdef ATARI_ST
extern "C" void WSA_Atari_TryInstallC2PWeights(const char *wsa_filename);
#endif


//
// WSA animation header allocation type.
//
#define WSA_USER_ALLOCATED     0x01
#define WSA_SYS_ALLOCATED      0x02
#define WSA_FILE               0x04
#define WSA_RESIDENT           0x08
#define WSA_TARGET_IN_BUFFER   0x10
#define WSA_LINEAR_ONLY        0x20
#define WSA_FRAME_0_ON_PAGE    0x40
#define WSA_AMIGA_ANIMATION    0x80
#define WSA_PALETTE_PRESENT    0x100
#define WSA_FRAME_0_IS_DELTA   0x200
/* C&C WSA header DeltaBufferSize is historically undersized by ~37 bytes. */
#define WSA_DELTA_UNDERSIZE_BIAS 37
// Used to call Apply_XOR_Delta_To_Page_Or_Viewport().
#define DO_XOR                 0x0
#define DO_COPY                0x01

typedef struct {
	unsigned short current_frame;
	unsigned short total_frames;
	unsigned short pixel_x;
	unsigned short pixel_y;
	unsigned short pixel_width;
	unsigned short pixel_height;
	unsigned short largest_frame_size;
	char *delta_buffer;
	char *file_buffer;
	char file_name[13];
	short flags;
	// New fields that animate does not know about below this point.
	short file_handle;
	unsigned long anim_mem_size;
} SysAnimHeaderType;

#ifdef ATARI_ST
extern "C" void Install_Animation_C2P_WeightSet(void *handle)
{
	SysAnimHeaderType *sys_header;

	if (!handle) {
		return;
	}
	sys_header = (SysAnimHeaderType *)handle;
	if (!sys_header->file_name[0]) {
		return;
	}
	WSA_Atari_TryInstallC2PWeights(sys_header->file_name);
}
#endif

// Keep compatibility with historical ANIMATE tool behavior.
#define EXTRA_CHARS_ANIMATE_NOT_KNOW_ABOUT (sizeof(short) + sizeof(unsigned long))

typedef struct {
	unsigned short total_frames;
	unsigned short pixel_x;
	unsigned short pixel_y;
	unsigned short pixel_width;
	unsigned short pixel_height;
	unsigned short largest_frame_size;
	short flags;
	unsigned long frame0_offset;
	unsigned long frame0_end;
	/* unsigned long data_seek_offset, unsigned short frame_size ... */
} WSA_FileHeaderType;

#define WSA_FILE_HEADER_SIZE (sizeof(WSA_FileHeaderType) - (2 * sizeof(unsigned long)))

PRIVATE unsigned long Get_Resident_Frame_Offset(char *file_buffer, int frame);
PRIVATE unsigned long Get_File_Frame_Offset(int file_handle, int frame, int palette_adjust);
PRIVATE BOOL Apply_Delta(SysAnimHeaderType *sys_header, int curr_frame, char *dest_ptr, int dest_w);

#define XORDELTA_BOUND_CHECK(CMD, OP_AT, OPERAND, WORDLE)                                 \
	do {                                                                              \
		if (frame_bytes != 0) {                                                   \
			size_t _pos = (size_t)(t - target);                               \
			size_t _op = (size_t)(OPERAND);                                   \
			assert(_pos + _op <= (size_t)frame_bytes);                        \
			if (_pos + _op > (size_t)frame_bytes) {                         \
				return 0;                                                     \
			}                                                             \
		}                                                                         \
	} while (0)

static inline uint16_t ReadLE16_u8(const unsigned char *p)
{
	return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t ReadLE32_u8(const unsigned char *p)
{
	return (uint32_t)p[0] |
	       ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

#ifdef ATARI_ST
#define APPLY_XOR_DELTA_LINEAR(target, delta) Apply_XOR_Delta((target), (delta), 0)
#define APPLY_XOR_DELTA_LINEAR_BOUNDED(target, delta, frame_bytes) Apply_XOR_Delta((target), (delta), (frame_bytes))
#else
#define APPLY_XOR_DELTA_LINEAR(target, delta) Apply_XOR_Delta((target), (delta))
#define APPLY_XOR_DELTA_LINEAR_BOUNDED(target, delta, frame_bytes) Apply_XOR_Delta((target), (delta))
#endif

#if defined(DEBUG) || defined(_DEBUG)
static const unsigned char *g_wsa_dbg_direct_begin = NULL;
static const unsigned char *g_wsa_dbg_direct_end = NULL; /* exclusive */
static int g_wsa_dbg_direct_enabled = 0;

static inline void WSA_Debug_Set_Direct_Bounds(const void *begin, unsigned long span_bytes)
{
	g_wsa_dbg_direct_begin = (const unsigned char *)begin;
	g_wsa_dbg_direct_end = g_wsa_dbg_direct_begin + span_bytes;
	g_wsa_dbg_direct_enabled = 1;
}

static inline void WSA_Debug_Clear_Direct_Bounds(void)
{
	g_wsa_dbg_direct_enabled = 0;
	g_wsa_dbg_direct_begin = NULL;
	g_wsa_dbg_direct_end = NULL;
}

static inline void WSA_Debug_Check_Direct_Ptr(const unsigned char *p, const char *site)
{
	if (!g_wsa_dbg_direct_enabled) {
		return;
	}
	if (p < g_wsa_dbg_direct_begin || p >= g_wsa_dbg_direct_end) {
		assert(0 && "WSA direct destination pointer out of bounds");
	}
}
#else
#define WSA_Debug_Set_Direct_Bounds(begin, span_bytes) ((void)0)
#define WSA_Debug_Clear_Direct_Bounds() ((void)0)
#define WSA_Debug_Check_Direct_Ptr(p, site) ((void)0)
#endif

extern "C" void *Open_Animation(char const *file_name, char *user_buffer, long user_buffer_size, WSAOpenType user_flags, unsigned char *palette)
{
	int fh, anim_flags;
	int palette_adjust;
	unsigned int offsets_size;
	unsigned int frame0_size;
	long target_buffer_size, delta_buffer_size, file_buffer_size;
	long max_buffer_size, min_buffer_size;
	char *sys_anim_header_buffer;
	char *target_buffer;
	char *delta_buffer, *delta_back;
	SysAnimHeaderType *sys_header;
	unsigned char header_bytes[WSA_FILE_HEADER_SIZE];
	unsigned short file_total_frames;
	unsigned short file_pixel_x;
	unsigned short file_pixel_y;
	unsigned short file_pixel_width;
	unsigned short file_pixel_height;
	unsigned short file_largest_frame_size;
	unsigned short file_flags;
	unsigned long file_frame0_offset;
	unsigned long file_frame0_end;

	anim_flags = 0;
	fh = Open_File(file_name, READ);
	if (fh < 0) {
		return NULL;
	}

	if (Read_File(fh, (char *)header_bytes, WSA_FILE_HEADER_SIZE) != WSA_FILE_HEADER_SIZE) {
		Close_File(fh);
		return NULL;
	}

	file_total_frames = ReadLE16_u8(header_bytes + 0);
	file_pixel_x = ReadLE16_u8(header_bytes + 2);
	file_pixel_y = ReadLE16_u8(header_bytes + 4);
	file_pixel_width = ReadLE16_u8(header_bytes + 6);
	file_pixel_height = ReadLE16_u8(header_bytes + 8);
	file_largest_frame_size = ReadLE16_u8(header_bytes + 10);
	file_flags = ReadLE16_u8(header_bytes + 12);

	if (file_flags & 1) {
		anim_flags |= WSA_PALETTE_PRESENT;
		palette_adjust = 768;

		if (palette != NULL) {
			/*
			 * C&C WSA stores (total_frames + 2) 32-bit offsets after the 14-byte header.
			 * Palette starts after the full offset table.
			 */
			Seek_File(fh, sizeof(unsigned long) * (file_total_frames + 2), SEEK_CUR);
			Read_File(fh, (char *)palette, 768L);
		}
	} else {
		palette_adjust = 0;
	}

	// Flag from ANIMATE indicating frame 0 is XOR delta from base image.
	if (file_flags & 2) {
		anim_flags |= WSA_FRAME_0_IS_DELTA;
	}

	file_buffer_size = Seek_File(fh, 0L, SEEK_END);

	Seek_File(fh, WSA_FILE_HEADER_SIZE, SEEK_SET);
	{
		unsigned char frame0_info[8];
		if (Read_File(fh, (char *)frame0_info, 8) == 8) {
			file_frame0_offset = ReadLE32_u8(frame0_info + 0);
			file_frame0_end = ReadLE32_u8(frame0_info + 4);
		} else {
			file_frame0_offset = 0;
			file_frame0_end = 0;
		}
	}

	if (file_frame0_offset) {
		long tlong = (long)(file_frame0_end - file_frame0_offset);
		frame0_size = (unsigned short)tlong;
	} else {
		anim_flags |= WSA_FRAME_0_ON_PAGE;
		frame0_size = 0;
	}

	file_buffer_size -= palette_adjust + frame0_size + WSA_FILE_HEADER_SIZE;

	if (user_flags & WSA_OPEN_DIRECT) {
		target_buffer_size = 0L;
	} else {
		anim_flags |= WSA_TARGET_IN_BUFFER;
		target_buffer_size = (unsigned long)file_pixel_width * file_pixel_height;
	}

	delta_buffer_size = (unsigned long)file_largest_frame_size +
	                    (unsigned long)WSA_DELTA_UNDERSIZE_BIAS +
	                    EXTRA_CHARS_ANIMATE_NOT_KNOW_ABOUT;
	min_buffer_size = target_buffer_size + delta_buffer_size;
	max_buffer_size = min_buffer_size + file_buffer_size;

	if (user_buffer && (user_buffer_size < min_buffer_size)) {
		Close_File(fh);
		return NULL;
	}

	if (user_buffer == NULL) {
		if (user_flags & WSA_OPEN_FROM_DISK) {
			user_buffer_size = min_buffer_size;
		} else if (!user_buffer_size) {
			user_buffer_size = max_buffer_size;
		} else if (user_buffer_size < max_buffer_size) {
			user_buffer_size = min_buffer_size;
		} else {
			user_buffer_size = max_buffer_size;
		}

		if (user_buffer_size > Ram_Free(MEM_NORMAL)) {
			if (min_buffer_size > Ram_Free(MEM_NORMAL)) {
				Close_File(fh);
				return NULL;
			}
			user_buffer_size = min_buffer_size;
		}

		user_buffer = (char *)Alloc(user_buffer_size, MEM_CLEAR);
		anim_flags |= WSA_SYS_ALLOCATED;
	} else {
		if ((user_flags & WSA_OPEN_FROM_DISK) || (user_buffer_size < max_buffer_size)) {
			user_buffer_size = min_buffer_size;
		} else {
			user_buffer_size = max_buffer_size;
		}
		anim_flags |= WSA_USER_ALLOCATED;
	}

	sys_anim_header_buffer = user_buffer;
	target_buffer = (char *)Add_Long_To_Pointer(sys_anim_header_buffer, sizeof(SysAnimHeaderType));
	delta_buffer = (char *)Add_Long_To_Pointer(target_buffer, target_buffer_size);

	if (target_buffer_size) {
		memset(target_buffer, 0, (unsigned short)target_buffer_size);
	}

	sys_header = (SysAnimHeaderType *)sys_anim_header_buffer;
	sys_header->current_frame = file_total_frames;
	sys_header->total_frames = file_total_frames;
	sys_header->pixel_x = file_pixel_x;
	sys_header->pixel_y = file_pixel_y;
	sys_header->pixel_width = file_pixel_width;
	sys_header->pixel_height = file_pixel_height;
	sys_header->anim_mem_size = user_buffer_size;
	sys_header->delta_buffer = delta_buffer;
	sys_header->largest_frame_size = (unsigned short)(delta_buffer_size - sizeof(SysAnimHeaderType));

	strcpy(sys_header->file_name, file_name);

	offsets_size = (file_total_frames + 2) << 2;

	if (user_buffer_size == max_buffer_size) {
		sys_header->file_buffer = (char *)Add_Long_To_Pointer(delta_buffer, sys_header->largest_frame_size);
		Seek_File(fh, WSA_FILE_HEADER_SIZE, SEEK_SET);
		Read_File(fh, sys_header->file_buffer, offsets_size);
		Seek_File(fh, frame0_size + palette_adjust, SEEK_CUR);
		Read_File(fh, sys_header->file_buffer + offsets_size, file_buffer_size - offsets_size);

		if (Get_Resident_Frame_Offset(sys_header->file_buffer, sys_header->total_frames + 1)) {
			anim_flags |= WSA_RESIDENT;
		} else {
			anim_flags |= WSA_LINEAR_ONLY | WSA_RESIDENT;
		}
	} else {
		if (Get_File_Frame_Offset(fh, sys_header->total_frames + 1, palette_adjust)) {
			anim_flags |= WSA_FILE;
		} else {
			anim_flags |= WSA_LINEAR_ONLY | WSA_FILE;
		}
		sys_header->file_buffer = NULL;
	}

	delta_back = (char *)Add_Long_To_Pointer(delta_buffer, sys_header->largest_frame_size - frame0_size);
	Seek_File(fh, WSA_FILE_HEADER_SIZE + offsets_size + palette_adjust, SEEK_SET);
	Read_File(fh, delta_back, frame0_size);

	if (anim_flags & WSA_RESIDENT) {
		sys_header->file_handle = (short)-1;
		Close_File(fh);
	} else {
		sys_header->file_handle = (short)fh;
	}

	LCW_Uncompress(delta_back, delta_buffer, sys_header->largest_frame_size);
	sys_header->flags = (short)anim_flags;
#ifdef ATARI_ST
	if ((user_flags & WSA_DEFERRED_C2P_WEIGHTSET) == 0) {
		WSA_Atari_TryInstallC2PWeights(file_name);
	}
#endif
	return user_buffer;
}

extern "C" void Close_Animation(void *handle)
{
	SysAnimHeaderType *sys_header = (SysAnimHeaderType *)handle;
	if (!handle) {
		return;
	}

	if (sys_header->flags & WSA_FILE) {
		Close_File(sys_header->file_handle);
	}
	if (sys_header->flags & WSA_SYS_ALLOCATED) {
		Free(handle);
	}
}

extern "C" BOOL Animate_Frame(void *handle, GraphicViewPortClass& view, int frame_number, int x_pixel, int y_pixel, WSAType flags_and_prio, void *magic_cols, void *magic)
{
	SysAnimHeaderType *sys_header;
	int curr_frame;
	int total_frames;
	int distance;
	int search_dir;
	int search_frames;
	int loop;
	char *frame_buffer;
	BOOL direct_to_dest;
	int dest_width;

	(void)flags_and_prio;
	(void)magic_cols;
	(void)magic;

	if (!handle) {
		return FALSE;
	}
	sys_header = (SysAnimHeaderType *)handle;
	total_frames = sys_header->total_frames;
	if (total_frames <= frame_number) {
		return FALSE;
	}

	if (view.Lock() != TRUE) {
		return FALSE;
	}

	dest_width = view.Get_Width() + view.Get_XAdd() + view.Get_Pitch();
	x_pixel += (short)sys_header->pixel_x;
	y_pixel += (short)sys_header->pixel_y;

	if (sys_header->flags & WSA_TARGET_IN_BUFFER) {
		frame_buffer = (char *)Add_Long_To_Pointer(sys_header, sizeof(SysAnimHeaderType));
		direct_to_dest = FALSE;
	} else {
		frame_buffer = (char *)view.Get_Offset();
		frame_buffer += (y_pixel * dest_width) + x_pixel;
		direct_to_dest = TRUE;
	}

#ifdef ATARI_ST
	/*
	 * Direct XOR decode uses nextrow = dest_width while each row only touches
	 * pixel_width bytes (see Apply_XOR_Delta_To_Page_Or_Viewport). Row stride
	 * may be larger than the anim width (e.g. 320 SysMemPage vs narrower WSA).
	 * Reject only when the viewport cannot hold one full scanline of the anim.
	 */
	if (direct_to_dest && dest_width < (int)sys_header->pixel_width) {
		assert(0 && "WSA direct decode: viewport row stride < animation width");
		view.Unlock();
		return FALSE;
	}
#endif

	if (sys_header->current_frame == total_frames) {
		if (!(sys_header->flags & WSA_FRAME_0_ON_PAGE)) {
			if (direct_to_dest) {
#if defined(DEBUG) || defined(_DEBUG)
				const unsigned long direct_span =
					((unsigned long)sys_header->pixel_height - 1UL) * (unsigned long)dest_width +
					(unsigned long)sys_header->pixel_width;
				WSA_Debug_Set_Direct_Bounds(frame_buffer, direct_span);
#endif
				Apply_XOR_Delta_To_Page_Or_Viewport(frame_buffer,
				                                    sys_header->delta_buffer,
				                                    sys_header->pixel_width,
				                                    dest_width,
				                                    (sys_header->flags & WSA_FRAME_0_IS_DELTA) ? DO_XOR : DO_COPY);
#if defined(DEBUG) || defined(_DEBUG)
				WSA_Debug_Clear_Direct_Bounds();
#endif
			} else {
#if defined(DEBUG) || defined(_DEBUG)
				const unsigned int guarded_frame_bytes =
					(unsigned int)((const char *)sys_header->delta_buffer - (const char *)frame_buffer);
				APPLY_XOR_DELTA_LINEAR_BOUNDED(frame_buffer, sys_header->delta_buffer, guarded_frame_bytes);
#else
				APPLY_XOR_DELTA_LINEAR(frame_buffer, sys_header->delta_buffer);
#endif
			}
		}
		sys_header->current_frame = 0;
	}

	curr_frame = sys_header->current_frame;
	distance = ABS(curr_frame - frame_number);
	search_dir = 1;

	if (frame_number > curr_frame) {
		search_frames = total_frames - frame_number + curr_frame;
		if ((search_frames < distance) && !(sys_header->flags & WSA_LINEAR_ONLY)) {
			search_dir = -1;
		} else {
			search_frames = distance;
		}
	} else {
		search_frames = total_frames - curr_frame + frame_number;
		if ((search_frames >= distance) || (sys_header->flags & WSA_LINEAR_ONLY)) {
			search_dir = -1;
			search_frames = distance;
		}
	}

	if (search_dir > 0) {
		for (loop = 0; loop < search_frames; loop++) {
			curr_frame += search_dir;
			const int decode_stride = direct_to_dest ? dest_width : (int)sys_header->pixel_width;
			if (!Apply_Delta(sys_header, curr_frame, frame_buffer, decode_stride)) {
				view.Unlock();
				return FALSE;
			}
			if (curr_frame == total_frames) {
				curr_frame = 0;
			}
		}
	} else {
		for (loop = 0; loop < search_frames; loop++) {
			if (curr_frame == 0) {
				curr_frame = total_frames;
			}
			const int decode_stride = direct_to_dest ? dest_width : (int)sys_header->pixel_width;
			if (!Apply_Delta(sys_header, curr_frame, frame_buffer, decode_stride)) {
				view.Unlock();
				return FALSE;
			}
			curr_frame += search_dir;
		}
	}

	sys_header->current_frame = (short)frame_number;

	if (sys_header->flags & WSA_TARGET_IN_BUFFER) {
		Buffer_To_Page(x_pixel, y_pixel, sys_header->pixel_width, sys_header->pixel_height, frame_buffer, view);
	}

	view.Unlock();
	return TRUE;
}

extern "C" int Get_Animation_Frame_Count(void *handle)
{
	SysAnimHeaderType *sys_header;
	if (!handle) {
		return FALSE;
	}
	sys_header = (SysAnimHeaderType *)handle;
	return (short)sys_header->total_frames;
}

extern "C" int Get_Animation_X(void const *handle)
{
	SysAnimHeaderType const *sys_header;
	if (!handle) {
		return FALSE;
	}
	sys_header = (SysAnimHeaderType const *)handle;
	return sys_header->pixel_x;
}

extern "C" int Get_Animation_Y(void const *handle)
{
	SysAnimHeaderType const *sys_header;
	if (!handle) {
		return FALSE;
	}
	sys_header = (SysAnimHeaderType const *)handle;
	return sys_header->pixel_y;
}

extern "C" int Get_Animation_Width(void const *handle)
{
	SysAnimHeaderType const *sys_header;
	if (!handle) {
		return FALSE;
	}
	sys_header = (SysAnimHeaderType const *)handle;
	return sys_header->pixel_width;
}

extern "C" int Get_Animation_Height(void const *handle)
{
	SysAnimHeaderType const *sys_header;
	if (!handle) {
		return FALSE;
	}
	sys_header = (SysAnimHeaderType const *)handle;
	return sys_header->pixel_height;
}

extern "C" int Get_Animation_Palette(void const *handle)
{
	SysAnimHeaderType const *sys_header;
	if (!handle) {
		return FALSE;
	}
	sys_header = (SysAnimHeaderType const *)handle;
	return (sys_header->flags & WSA_PALETTE_PRESENT);
}

extern "C" unsigned long Get_Animation_Size(void const *handle)
{
	SysAnimHeaderType const *sys_header;
	if (!handle) {
		return FALSE;
	}
	sys_header = (SysAnimHeaderType const *)handle;
	return sys_header->anim_mem_size;
}

#ifdef ATARI_ST
extern "C" unsigned int Apply_XOR_Delta(char *target, char *delta, unsigned int frame_bytes)
#else
extern "C" unsigned int Apply_XOR_Delta(char *target, char *delta)
#endif
{
	char *t = target;
	char *d = delta;
	unsigned int bytes_processed = 0;
#ifndef ATARI_ST
	unsigned int frame_bytes = 0;
#endif

	if (!target || !delta) {
		return 0;
	}

	while (1) {
		unsigned char code = (unsigned char)*d++;
		bytes_processed++;

		// SHORTDUMP (0 < code < 128)
		if (code > 0 && code < 128) {
			unsigned int count = code;
			XORDELTA_BOUND_CHECK("SHORTDUMP", d - 1, count, 0);
			for (unsigned int i = 0; i < count; i++) {
				*t++ ^= *d++;
				bytes_processed++;
			}
			continue;
		}

		// SHORTRUN (code == 0)
		if (code == 0) {
			unsigned char count = (unsigned char)*d++;
			unsigned char value = (unsigned char)*d++;
			bytes_processed += 2;

			XORDELTA_BOUND_CHECK("SHORTRUN", d - 3, (unsigned int)count, 0);
			for (unsigned int i = 0; i < (unsigned int)count; i++) {
				*t++ ^= (char)value;
			}
			continue;
		}

		// SHORTSKIP (128 < code <= 255)
		if (code > 128) {
			unsigned int skip = code - 128;
			XORDELTA_BOUND_CHECK("SHORTSKIP", d - 1, skip, 0);
			t += skip;
			continue;
		}

		// code == 128: read little-endian word
		{
			unsigned short word_code = ReadLE16_u8((const unsigned char *)d);
			d += 2;
			bytes_processed += 2;

			if (word_code == 0) {
				break;
			}

			if (word_code < 0x8000u) {
				unsigned int skip = (unsigned int)word_code;
				XORDELTA_BOUND_CHECK("LONGSKIP", d - 3, skip, (unsigned int)word_code);
				t += skip;
				continue;
			}

			// w >= 0x8000: same split as WIN32LIB/XORDELTA.ASM
			{
				unsigned int X = (unsigned int)word_code - 0x8000u;
				if ((X & 0x4000u) == 0u) {
					// LONGDUMP
					unsigned int count = X;
					XORDELTA_BOUND_CHECK("LONGDUMP", d - 3, count, (unsigned int)word_code);
					for (unsigned int i = 0; i < count; i++) {
						*t++ ^= *d++;
						bytes_processed++;
					}
				} else {
					// LONGRUN
					unsigned int count = X - 0x4000u;
					unsigned char value = (unsigned char)*d++;
					bytes_processed++;
					XORDELTA_BOUND_CHECK("LONGRUN", d - 4, count, (unsigned int)word_code);
					for (unsigned int i = 0; i < count; i++) {
						*t++ ^= (char)value;
					}
				}
			}
		}
	}

	return bytes_processed;
}

extern "C" void Apply_XOR_Delta_To_Page_Or_Viewport(void *target, void *delta, int width, int nextrow, int copy)
{
	unsigned char *dst = (unsigned char *)target;
	const unsigned char *src = (const unsigned char *)delta;
	int col = 0;
	int is_copy = (copy != DO_XOR);

	if (!dst || !src || width <= 0 || nextrow <= 0) {
		return;
	}

	while (1) {
		unsigned int code = *src++;

		/* SHORTDUMP */
		if (code > 0 && code < 128) {
			unsigned int count = code;
			while (count--) {
				WSA_Debug_Check_Direct_Ptr(dst, "SHORTDUMP");
				unsigned char v = *src++;
				if (is_copy) {
					*dst = v;
				} else {
					*dst ^= v;
				}

				dst++;
				col++;
				if (col == width) {
					dst -= width;
					col = 0;
					dst += nextrow;
				}
			}
			continue;
		}

		/* SHORTRUN */
		if (code == 0) {
			unsigned int count = *src++;
			unsigned char v = *src++;
			while (count--) {
				WSA_Debug_Check_Direct_Ptr(dst, "SHORTRUN");
				if (is_copy) {
					*dst = v;
				} else {
					*dst ^= v;
				}

				dst++;
				col++;
				if (col == width) {
					dst -= width;
					col = 0;
					dst += nextrow;
				}
			}
			continue;
		}

		/* SHORTSKIP */
		if (code > 128) {
			unsigned int skip = code - 128;
			dst -= col;
			col += (int)skip;
			while (col >= width) {
				col -= width;
				dst += nextrow;
			}
			dst += col;
			continue;
		}

		/* code == 128: LONGSKIP / LONGDUMP / LONGRUN / STOP */
		{
			unsigned int word_code = (unsigned int)ReadLE16_u8(src);
			src += 2;

			if (word_code == 0) {
				break;
			}

			if (word_code < 0x8000u) {
				unsigned int skip = word_code;
				dst -= col;
				col += (int)skip;
				while (col >= width) {
					col -= width;
					dst += nextrow;
				}
				dst += col;
				continue;
			}

			{
				unsigned int X = word_code - 0x8000u;
				if ((X & 0x4000u) == 0u) {
					/* LONGDUMP */
					unsigned int count = X;
					while (count--) {
						WSA_Debug_Check_Direct_Ptr(dst, "LONGDUMP");
						unsigned char v = *src++;
						if (is_copy) {
							*dst = v;
						} else {
							*dst ^= v;
						}

						dst++;
						col++;
						if (col == width) {
							dst -= width;
							col = 0;
							dst += nextrow;
						}
					}
				} else {
					/* LONGRUN */
					unsigned int count = X - 0x4000u;
					unsigned char v = *src++;
					while (count--) {
						WSA_Debug_Check_Direct_Ptr(dst, "LONGRUN");
						if (is_copy) {
							*dst = v;
						} else {
							*dst ^= v;
						}

						dst++;
						col++;
						if (col == width) {
							dst -= width;
							col = 0;
							dst += nextrow;
						}
					}
				}
			}
		}
	}
}

PRIVATE unsigned long Get_Resident_Frame_Offset(char *file_buffer, int frame)
{
	unsigned long frame0_size;
	const unsigned char *base = (const unsigned char *)file_buffer;
	unsigned long first = ReadLE32_u8(base);

	if (first) {
		unsigned long second = ReadLE32_u8(base + 4);
		frame0_size = second - first;
	} else {
		frame0_size = 0;
	}

	{
		unsigned long off = ReadLE32_u8(base + (frame << 2));
		if (off) {
			return off - (frame0_size + WSA_FILE_HEADER_SIZE);
		}
	}
	return 0L;
}

PRIVATE unsigned long Get_File_Frame_Offset(int file_handle, int frame, int palette_adjust)
{
	unsigned char off_bytes[4];
	unsigned long offset_raw;

	Seek_File(file_handle, (frame << 2) + WSA_FILE_HEADER_SIZE, SEEK_SET);
	if (Read_File(file_handle, (char *)off_bytes, 4) != 4) {
		offset_raw = 0L;
	} else {
		offset_raw = ReadLE32_u8(off_bytes);
	}
	if (!offset_raw) {
		return 0L;
	}
	return offset_raw + (unsigned long)palette_adjust;
}

PRIVATE BOOL Apply_Delta(SysAnimHeaderType *sys_header, int curr_frame, char *dest_ptr, int dest_w)
{
	char *data_ptr, *delta_back;
	int file_handle, palette_adjust;
	unsigned long frame_data_size, frame_offset;

	palette_adjust = ((sys_header->flags & WSA_PALETTE_PRESENT) ? 768 : 0);
	delta_back = sys_header->delta_buffer;

	if (sys_header->flags & WSA_RESIDENT) {
		frame_offset = Get_Resident_Frame_Offset(sys_header->file_buffer, curr_frame);
		frame_data_size = Get_Resident_Frame_Offset(sys_header->file_buffer, curr_frame + 1) - frame_offset;
		data_ptr = (char *)Add_Long_To_Pointer(sys_header->file_buffer, frame_offset);
		delta_back = (char *)Add_Long_To_Pointer(delta_back, sys_header->largest_frame_size - frame_data_size);
		Mem_Copy(data_ptr, delta_back, frame_data_size);
	} else if (sys_header->flags & WSA_FILE) {
		file_handle = sys_header->file_handle;
		Seek_File(file_handle, 0L, SEEK_SET);

		frame_offset = Get_File_Frame_Offset(file_handle, curr_frame, palette_adjust);
		frame_data_size = Get_File_Frame_Offset(file_handle, curr_frame + 1, palette_adjust) - frame_offset;
		if (!frame_offset || !frame_data_size) {
			return FALSE;
		}

		Seek_File(file_handle, frame_offset, SEEK_SET);
		delta_back = (char *)Add_Long_To_Pointer(delta_back, sys_header->largest_frame_size - frame_data_size);
		if (Read_File(file_handle, delta_back, frame_data_size) != frame_data_size) {
			return FALSE;
		}
	}

	LCW_Uncompress(delta_back, sys_header->delta_buffer, sys_header->largest_frame_size);

	if (sys_header->flags & WSA_TARGET_IN_BUFFER) {
#if defined(DEBUG) || defined(_DEBUG)
		const unsigned int guarded_frame_bytes =
			(unsigned int)((const char *)sys_header->delta_buffer - (const char *)dest_ptr);
		APPLY_XOR_DELTA_LINEAR_BOUNDED(dest_ptr, sys_header->delta_buffer, guarded_frame_bytes);
#else
		APPLY_XOR_DELTA_LINEAR(dest_ptr, sys_header->delta_buffer);
#endif
	} else {
#if defined(DEBUG) || defined(_DEBUG)
		const unsigned long direct_span =
			((unsigned long)sys_header->pixel_height - 1UL) * (unsigned long)dest_w +
			(unsigned long)sys_header->pixel_width;
		WSA_Debug_Set_Direct_Bounds(dest_ptr, direct_span);
#endif
		Apply_XOR_Delta_To_Page_Or_Viewport(dest_ptr, sys_header->delta_buffer, sys_header->pixel_width, dest_w, DO_XOR);
#if defined(DEBUG) || defined(_DEBUG)
		WSA_Debug_Clear_Direct_Bounds();
#endif
	}

	return TRUE;
}
