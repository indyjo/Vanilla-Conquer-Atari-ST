/*
 * wsa.cpp - WSA (Westwood Animation) and XOR Delta functions for Atari ST/MiNT
 * 
 * This provides implementations for WSA animation and XOR delta functions
 */

#include "wsa.h"
#include "file.h"
#include "rawfile.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Warn on XOR-delta linear buffer overruns (Build_Frame / SHP); caller aborts apply. */
static void XorDelta_LogBounds(
	const char *cmd,
	const char *target,
	const char *t,
	const char *delta0,
	const char *opcode_at,
	unsigned int frame_bytes,
	unsigned long operand,
	unsigned int bytes_processed,
	unsigned int word_le /* 0 if not a 0x80-prefix command */)
{
	unsigned long pos = (unsigned long)((const unsigned char *)t - (const unsigned char *)target);
	unsigned long op_off = (unsigned long)((const unsigned char *)opcode_at - (const unsigned char *)delta0);
	fprintf(stderr,
		"[Apply_XOR_Delta] %s: dest_pos=%lu opnd=%lu frame_bytes=%u stream_off=%lu "
		"bytes_in=%u target=%p delta0=%p op@=%p",
		cmd, pos, operand, frame_bytes, op_off, bytes_processed,
		(const void *)target, (const void *)delta0, (const void *)opcode_at);
	if (word_le != 0) {
		fprintf(stderr, " word_le=0x%04X", word_le & 0xFFFFu);
	}
	fprintf(stderr, "\n");
	fflush(stderr);
}

#define XORDELTA_BOUND_CHECK(CMD, OP_AT, OPERAND, WORDLE)                                 \
	do {                                                                              \
		if (frame_bytes != 0) {                                                   \
			size_t _pos = (size_t)(t - target);                               \
			size_t _op = (size_t)(OPERAND);                                   \
			if (_pos + _op > (size_t)frame_bytes) {                         \
				XorDelta_LogBounds((CMD), target, t, delta, (OP_AT),      \
					frame_bytes, (unsigned long)(OPERAND), bytes_processed, \
					(WORDLE));                                            \
				fprintf(stderr,                                               \
					"[Apply_XOR_Delta] Stopping XOR apply (buffer bounds).\n"); \
				fflush(stderr);                                               \
				return 0;                                                     \
			}                                                             \
		}                                                                         \
	} while (0)

/* Westwood XOR delta streams use little-endian 16-bit words; read by bytes so
 * this works on big-endian m68k and when the stream pointer is odd-aligned. */
static inline unsigned short ReadLE16_u8(const unsigned char *p)
{
	return (unsigned short)(p[0] | (p[1] << 8));
}

/*=========================================================================*/
/* WSA Animation Functions - Stub implementations                          */
/*=========================================================================*/

/* Forward declaration for WSA handle structure */
struct WSAHandle {
	char *filename;
	void *buffer;
	long buffer_size;
	int frame_count;
	int width;
	int height;
	unsigned char *palette;
};

/*=========================================================================*/
/* Open_Animation -- Opens an animation file                               */
/*=========================================================================*/
extern "C" void *Open_Animation(char const *file_name, char *user_buffer, long user_buffer_size, WSAOpenType user_flags, unsigned char *palette)
{
	// Stub implementation - returns a dummy handle
	// A full implementation would load the WSA file and parse its header
	(void)user_buffer;
	(void)user_buffer_size;
	(void)user_flags;
	
	WSAHandle *handle = (WSAHandle *)malloc(sizeof(WSAHandle));
	if (!handle) return NULL;
	
	memset(handle, 0, sizeof(WSAHandle));
	
	if (file_name) {
		handle->filename = strdup(file_name);
	}
	
	// Default values - would be read from WSA file header
	handle->frame_count = 1;
	handle->width = 320;
	handle->height = 200;
	
	if (palette) {
		handle->palette = palette;
	}
	
	return handle;
}

/*=========================================================================*/
/* Close_Animation -- Closes an animation                                  */
/*=========================================================================*/
extern "C" void Close_Animation(void *handle)
{
	if (!handle) return;
	
	WSAHandle *wsa = (WSAHandle *)handle;
	
	if (wsa->filename) {
		free(wsa->filename);
	}
	
	if (wsa->buffer) {
		free(wsa->buffer);
	}
	
	free(wsa);
}

/*=========================================================================*/
/* Animate_Frame -- Displays a frame of an animation                        */
/*=========================================================================*/
extern "C" BOOL Animate_Frame(void *handle, GraphicViewPortClass& view, int frame_number, int x_pixel, int y_pixel, WSAType flags_and_prio, void *magic_cols, void *magic)
{
	// Stub implementation
	(void)handle;
	(void)view;
	(void)frame_number;
	(void)x_pixel;
	(void)y_pixel;
	(void)flags_and_prio;
	(void)magic_cols;
	(void)magic;
	
	return FALSE;
}

/*=========================================================================*/
/* Get_Animation_Frame_Count -- Returns number of frames                   */
/*=========================================================================*/
extern "C" int Get_Animation_Frame_Count(void *handle)
{
	if (!handle) return 0;
	
	WSAHandle *wsa = (WSAHandle *)handle;
	return wsa->frame_count;
}

/*=========================================================================*/
/* Get_Animation_X -- Returns X position                                    */
/*=========================================================================*/
extern "C" int Get_Animation_X(void const *handle)
{
	if (!handle) return 0;
	return 0;
}

/*=========================================================================*/
/* Get_Animation_Y -- Returns Y position                                    */
/*=========================================================================*/
extern "C" int Get_Animation_Y(void const *handle)
{
	if (!handle) return 0;
	return 0;
}

/*=========================================================================*/
/* Get_Animation_Width -- Returns animation width                           */
/*=========================================================================*/
extern "C" int Get_Animation_Width(void const *handle)
{
	if (!handle) return 0;
	
	WSAHandle *wsa = (WSAHandle *)handle;
	return wsa->width;
}

/*=========================================================================*/
/* Get_Animation_Height -- Returns animation height                         */
/*=========================================================================*/
extern "C" int Get_Animation_Height(void const *handle)
{
	if (!handle) return 0;
	
	WSAHandle *wsa = (WSAHandle *)handle;
	return wsa->height;
}

/*=========================================================================*/
/* Get_Animation_Palette -- Returns palette pointer                        */
/*=========================================================================*/
extern "C" int Get_Animation_Palette(void const *handle)
{
	if (!handle) return 0;
	
	WSAHandle *wsa = (WSAHandle *)handle;
	return (int)wsa->palette;
}

/*=========================================================================*/
/* Get_Animation_Size -- Returns animation size                            */
/*=========================================================================*/
extern "C" unsigned long Get_Animation_Size(void const *handle)
{
	if (!handle) return 0;
	
	WSAHandle *wsa = (WSAHandle *)handle;
	return wsa->buffer_size;
}

/*=========================================================================*/
/* Apply_XOR_Delta -- Apply XOR delta data to a linear buffer              */
/*                                                                         */
/* This is a portable C implementation translated from x86 assembly       */
/*                                                                         */
/* Command format:                                                          */
/*   n = 0-127: SHORTDUMP - copy next n bytes, XORing each                 */
/*   n = 0: SHORTRUN - run of next byte, count from next byte              */
/*   n = 128-255: SHORTSKIP - skip n-128 bytes                             */
/*   n = 128, w = word: LONGSKIP - skip w bytes (if w > 0)                */
/*   n = 128, w = 0: STOP - end of data                                   */
/* After n==128, 16-bit w (LE): if 0<w<0x8000, LONGSKIP w bytes.           */
/* If w>=0x8000: let X=w-0x8000; if (X&0x4000)==0, LONGDUMP X XOR bytes;  */
/* else LONGRUN: XOR one byte value (X-0x4000) times (matches XORDELTA.ASM).*/
/*                                                                         */
/* INPUT:                                                                  */
/*   target -- Destination buffer                                         */
/*   delta  -- XOR delta data to apply                                     */
/*   frame_bytes -- width*height for bounds checks; 0 = skip bounds checks */
/*                                                                         */
/* OUTPUT:                                                                 */
/*   Returns number of bytes processed (or 0 on error)                    */
/*=========================================================================*/
extern "C" unsigned int Apply_XOR_Delta(char *target, char *delta, unsigned int frame_bytes)
{
	char *t = target;
	char *d = delta;
	unsigned int bytes_processed = 0;

	if (!target || !delta) {
		return 0;
	}

	while (1) {
		unsigned char code = (unsigned char)*d++;
		bytes_processed++;

		// Check for SHORTDUMP (0 < code < 128)
		if (code > 0 && code < 128) {
			unsigned int count = code;
			XORDELTA_BOUND_CHECK("SHORTDUMP", d - 1, count, 0);
			for (unsigned int i = 0; i < count; i++) {
				*t++ ^= *d++;
				bytes_processed++;
			}
			continue;
		}

		// Check for SHORTRUN (code == 0)
		if (code == 0) {
			unsigned char count = (unsigned char)*d++;
			unsigned char value = *d++;
			bytes_processed += 2;

			XORDELTA_BOUND_CHECK("SHORTRUN", d - 3, (unsigned int)(unsigned char)count, 0);
			for (unsigned int i = 0; i < (unsigned int)count; i++) {
				*t++ ^= value;
			}
			continue;
		}

		// Check for SHORTSKIP (128 <= code < 256, code != 128)
		if (code > 128) {
			unsigned int skip = code - 128;
			XORDELTA_BOUND_CHECK("SHORTSKIP", d - 1, skip, 0);
			t += skip;
			continue;
		}

		// code == 128: get next word (little-endian, possibly odd-aligned)
		unsigned short word_code = ReadLE16_u8((const unsigned char *)d);
		d += 2;
		bytes_processed += 2;

		if (word_code == 0) {
			break;
		}

		if (word_code > 0 && word_code < 0x8000) {
			unsigned int skip = word_code;
			XORDELTA_BOUND_CHECK("LONGSKIP", d - 3, skip, (unsigned int)word_code);
			t += skip;
			continue;
		}

		/* w >= 0x8000: same split as WIN32LIB/XORDELTA.ASM (not "w < 0xC000" LONGRUN) */
		{
			unsigned int X = (unsigned int)word_code - 0x8000u;
			if ((X & 0x4000u) == 0u) {
				/* LONGDUMP */
				unsigned int count = X;
				XORDELTA_BOUND_CHECK("LONGDUMP", d - 3, count, (unsigned int)word_code);
				for (unsigned int i = 0; i < count; i++) {
					*t++ ^= *d++;
					bytes_processed++;
				}
			} else {
				/* LONGRUN */
				unsigned int count = X - 0x4000u;
				unsigned char value = *d++;
				bytes_processed++;
				XORDELTA_BOUND_CHECK("LONGRUN", d - 4, count, (unsigned int)word_code);
				for (unsigned int i = 0; i < count; i++) {
					*t++ ^= value;
				}
			}
			continue;
		}
	}

	return bytes_processed;
}

/*=========================================================================*/
/* Apply_XOR_Delta_To_Page_Or_Viewport -- Apply XOR delta to page/viewport */
/*                                                                         */
/* Stub implementation - not fully implemented yet                        */
/*=========================================================================*/
extern "C" void Apply_XOR_Delta_To_Page_Or_Viewport(void *target, void *delta, int width, int nextrow, int copy)
{
	// Stub implementation
	// A full implementation would handle page/viewport boundaries
	(void)width;
	(void)nextrow;
	(void)copy;
	
	if (copy) {
		// Copy mode - just copy delta data
		// Not implemented yet
	} else {
		// XOR mode - apply XOR delta
		Apply_XOR_Delta((char *)target, (char *)delta, 0);
	}
}

