#include "st_planar_draw.h"
#include <stddef.h>

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

void ST_Planar_Draw_HLine_Fast(
	uint16_t *planar_root,
	short planar_row_words,
	short y_abs,
	short x1_abs,
	short x2_abs,
	uint16_t color4)
{
	/* Resolve one destination row and the 16-pixel word range it touches. */
	uint16_t *row = planar_root + (size_t)y_abs * (size_t)planar_row_words;
	const uint8_t c = (uint8_t)color4;
	const short w1 = (short)(x1_abs >> 4);
	const short w2 = (short)(x2_abs >> 4);
	const short b1 = (short)(x1_abs & 15);
	const short b2 = (short)(x2_abs & 15);
	const uint16_t *fills = kFillWordsByNibble[c];
	const uint16_t fill0 = fills[0];
	const uint16_t fill1 = fills[1];
	const uint16_t fill2 = fills[2];
	const uint16_t fill3 = fills[3];
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

	/* Middle words are fully covered: write all plane words directly. */
	for (uint16_t *p = start + 4; p < end; p += 4) {
		p[0] = fill0;
		p[1] = fill1;
		p[2] = fill2;
		p[3] = fill3;
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
