/*
 * vqa_format.c - VQA format helpers for vqatool.
 */
#include "vqa_format.h"

#include <stdio.h>
#include <string.h>

const char *vqa_chunk_name(uint32_t id)
{
	static char buf[5];
	unsigned i;

	for (i = 0; i < 4; i++) {
		unsigned char c = (unsigned char)((id >> (8u * i)) & 0xffu);
		buf[i] = (c >= 32 && c < 127) ? (char)c : '?';
	}
	buf[4] = '\0';
	return buf;
}

const char *vqa_color_mode_name(uint8_t mode)
{
	switch (mode) {
	case 0:
		return "256";
	case 1:
		return "15-bit";
	case 4:
		return "16-bit";
	default:
		return "unknown";
	}
}

void vqa_format_flags(uint16_t flags, char *buf, unsigned buf_len)
{
	unsigned n = 0;
	buf[0] = '\0';

	if (flags & 1u) {
		n += (unsigned)snprintf(buf + n, buf_len > n ? buf_len - n : 0, "%shas_sound", n ? "|" : "");
	}
	if (flags & 2u) {
		n += (unsigned)snprintf(buf + n, buf_len > n ? buf_len - n : 0, "%salt_audio", n ? "|" : "");
	}
	if (flags & 4u) {
		n += (unsigned)snprintf(buf + n, buf_len > n ? buf_len - n : 0, "%stransparent_bg", n ? "|" : "");
	}
	if (flags & 8u) {
		n += (unsigned)snprintf(buf + n, buf_len > n ? buf_len - n : 0, "%sflag_8", n ? "|" : "");
	}
	if (flags & 16u) {
		n += (unsigned)snprintf(buf + n, buf_len > n ? buf_len - n : 0, "%sflag_16", n ? "|" : "");
	}
	if (n == 0) {
		snprintf(buf, buf_len, "(none)");
	}
}

void vqa_sanitize_vga6_palette(unsigned char pal[VQA_PALETTE_BYTES])
{
	unsigned i;
	if (!pal)
		return;
	for (i = 0; i < VQA_PALETTE_BYTES; i++)
		pal[i] = (unsigned char)(pal[i] & 63u);
}
