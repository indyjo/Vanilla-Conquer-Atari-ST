#include "st_planar_draw.h"
#include <stddef.h>
#include <stdint.h>

/* Per-nibble plane fills (0xFFFF when plane bit is set, else 0x0000). */
static const uint16_t kFillWordsByNibble[16][4] = {
	{0x0000, 0x0000, 0x0000, 0x0000}, {0xFFFF, 0x0000, 0x0000, 0x0000},
	{0x0000, 0xFFFF, 0x0000, 0x0000}, {0xFFFF, 0xFFFF, 0x0000, 0x0000},
	{0x0000, 0x0000, 0xFFFF, 0x0000}, {0xFFFF, 0x0000, 0xFFFF, 0x0000},
	{0x0000, 0xFFFF, 0xFFFF, 0x0000}, {0xFFFF, 0xFFFF, 0xFFFF, 0x0000},
	{0x0000, 0x0000, 0x0000, 0xFFFF}, {0xFFFF, 0x0000, 0x0000, 0xFFFF},
	{0x0000, 0xFFFF, 0x0000, 0xFFFF}, {0xFFFF, 0xFFFF, 0x0000, 0xFFFF},
	{0x0000, 0x0000, 0xFFFF, 0xFFFF}, {0xFFFF, 0x0000, 0xFFFF, 0xFFFF},
	{0x0000, 0xFFFF, 0xFFFF, 0xFFFF}, {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}
};

static const uint16_t kLeftMask[16] = {
	0xFFFF, 0x7FFF, 0x3FFF, 0x1FFF, 0x0FFF, 0x07FF, 0x03FF, 0x01FF,
	0x00FF, 0x007F, 0x003F, 0x001F, 0x000F, 0x0007, 0x0003, 0x0001
};
static const uint16_t kRightMask[16] = {
	0x8000, 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00,
	0xFF80, 0xFFC0, 0xFFE0, 0xFFF0, 0xFFF8, 0xFFFC, 0xFFFE, 0xFFFF
};

/*
 * One scanline of flat color: x inclusive in [x1_abs,x2_abs], row = start of that line.
 * w1,w2,b1,b2 are derived from x1_abs,x2_abs (16-pixel word indices and in-word bits).
 */
static void Apply_Flat_HSpan_To_Row(
	uint16_t *row,
	short w1,
	short w2,
	short b1,
	short b2,
	uint16_t fill0,
	uint16_t fill1,
	uint16_t fill2,
	uint16_t fill3)
{
	uint16_t *start = row + (size_t)w1 * 4u;
	uint16_t *end = row + (size_t)w2 * 4u;

	/* Span fits in one word: apply one combined edge mask to all planes. */
	if (w1 == w2) {
		const uint16_t mask = (uint16_t)(kLeftMask[b1] & kRightMask[b2]);
		const uint16_t inv = (uint16_t)~mask;
		start[0] = (uint16_t)((start[0] & inv) | (fill0 & mask));
		start[1] = (uint16_t)((start[1] & inv) | (fill1 & mask));
		start[2] = (uint16_t)((start[2] & inv) | (fill2 & mask));
		start[3] = (uint16_t)((start[3] & inv) | (fill3 & mask));
		return;
	}

	/* First word is partial: preserve bits left of the span. */
	{
		const uint16_t first_mask = kLeftMask[b1];
		const uint16_t inv = (uint16_t)~first_mask;
		start[0] = (uint16_t)((start[0] & inv) | (fill0 & first_mask));
		start[1] = (uint16_t)((start[1] & inv) | (fill1 & first_mask));
		start[2] = (uint16_t)((start[2] & inv) | (fill2 & first_mask));
		start[3] = (uint16_t)((start[3] & inv) | (fill3 & first_mask));
	}

	/* Middle words are fully covered: write all plane words for each 16-pixel group. */
	for (uint16_t *p = start + 4; p < end; p += 4) {
#if defined(__m68k__)
		/* Big-endian: two longs cover plane0..3 in order (matches four halfword stores). */
		uint32_t *pd = (uint32_t *)(void *)p;
		pd[0] = ((uint32_t)fill0 << 16) | (uint32_t)fill1;
		pd[1] = ((uint32_t)fill2 << 16) | (uint32_t)fill3;
#else
		p[0] = fill0;
		p[1] = fill1;
		p[2] = fill2;
		p[3] = fill3;
#endif
	}

	/* Last word is partial: preserve bits right of the span. */
	{
		const uint16_t last_mask = kRightMask[b2];
		const uint16_t inv = (uint16_t)~last_mask;
		end[0] = (uint16_t)((end[0] & inv) | (fill0 & last_mask));
		end[1] = (uint16_t)((end[1] & inv) | (fill1 & last_mask));
		end[2] = (uint16_t)((end[2] & inv) | (fill2 & last_mask));
		end[3] = (uint16_t)((end[3] & inv) | (fill3 & last_mask));
	}
}

void ST_Planar_Get_Fill_Words(uint16_t color4, uint16_t out[4])
{
	const uint8_t c = (uint8_t)color4;
	if (c > 15u || out == NULL) {
		return;
	}
	out[0] = kFillWordsByNibble[c][0];
	out[1] = kFillWordsByNibble[c][1];
	out[2] = kFillWordsByNibble[c][2];
	out[3] = kFillWordsByNibble[c][3];
}

void ST_Planar_Draw_HLine_Fast(
	uint16_t *planar_root,
	short planar_row_words,
	short y_abs,
	short x1_abs,
	short x2_abs,
	uint16_t color4)
{
	uint16_t *row = planar_root + (size_t)y_abs * (size_t)planar_row_words;
	const uint8_t c = (uint8_t)color4;
	const short w1 = (short)(x1_abs >> 4);
	const short w2 = (short)(x2_abs >> 4);
	const short b1 = (short)(x1_abs & 15);
	const short b2 = (short)(x2_abs & 15);
	const uint16_t *fills = kFillWordsByNibble[c];
	Apply_Flat_HSpan_To_Row(row, w1, w2, b1, b2, fills[0], fills[1], fills[2], fills[3]);
}

void ST_Planar_Fill_Rect_Fast(
	uint16_t *planar_root,
	short planar_row_words,
	short y1_abs,
	short y2_abs,
	short x1_abs,
	short x2_abs,
	uint16_t color4)
{
	if (!planar_root || planar_row_words <= 0)
		return;

	short xa = x1_abs;
	short xb = x2_abs;
	if (xa > xb) {
		short t = xa;
		xa = xb;
		xb = t;
	}
	short ya = y1_abs;
	short yb = y2_abs;
	if (ya > yb) {
		short t = ya;
		ya = yb;
		yb = t;
	}

	const uint8_t c = (uint8_t)color4;
	const short w1 = (short)(xa >> 4);
	const short w2 = (short)(xb >> 4);
	const short b1 = (short)(xa & 15);
	const short b2 = (short)(xb & 15);
	const uint16_t *fills = kFillWordsByNibble[c];
	const uint16_t fill0 = fills[0];
	const uint16_t fill1 = fills[1];
	const uint16_t fill2 = fills[2];
	const uint16_t fill3 = fills[3];

	for (short y = ya; y <= yb; ++y) {
		uint16_t *row = planar_root + (size_t)y * (size_t)planar_row_words;
		Apply_Flat_HSpan_To_Row(row, w1, w2, b1, b2, fill0, fill1, fill2, fill3);
	}
}

void ST_Planar_Draw_VLine_Fast(
	uint16_t *planar_root,
	short planar_row_words,
	short x_abs,
	short y1_abs,
	short y2_abs,
	uint16_t color4)
{
	/* For fixed x, precompute word offset and one bit mask. */
	const short word_off = (short)((x_abs >> 4) * 4);
	const uint16_t bitmask = (uint16_t)(1u << (15 - (x_abs & 15)));
	const uint16_t inv = (uint16_t)~bitmask;
	const short rows = (short)(y2_abs - y1_abs + 1);
	const uint16_t c = color4;
	const uint16_t use0 = (c & 0x1) ? bitmask : 0;
	const uint16_t use1 = (c & 0x2) ? bitmask : 0;
	const uint16_t use2 = (c & 0x4) ? bitmask : 0;
	const uint16_t use3 = (c & 0x8) ? bitmask : 0;
	uint16_t *p = planar_root + (size_t)y1_abs * (size_t)planar_row_words + (size_t)word_off;

	/* Process each row once and update all four plane words together. */
	for (short i = 0; i < rows; ++i, p += planar_row_words) {
		p[0] = (uint16_t)((p[0] & inv) | use0);
		p[1] = (uint16_t)((p[1] & inv) | use1);
		p[2] = (uint16_t)((p[2] & inv) | use2);
		p[3] = (uint16_t)((p[3] & inv) | use3);
	}
}
