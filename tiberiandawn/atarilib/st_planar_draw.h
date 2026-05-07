#ifndef ATARILIB_ST_PLANAR_DRAW_H
#define ATARILIB_ST_PLANAR_DRAW_H

#include <stdint.h>

/*
 * Fast horizontal span draw on ST interleaved planar memory.
 * Coordinates are absolute to planar_root and inclusive.
 * color4 must already be a valid ST nibble (0..15).
 */
void ST_Planar_Draw_HLine_Fast(
	uint16_t *planar_root,
	short planar_row_words,
	short y_abs,
	short x1_abs,
	short x2_abs,
	uint16_t color4);

/*
 * Fast vertical span draw on ST interleaved planar memory.
 * Coordinates are absolute to planar_root and inclusive.
 * color4 must already be a valid ST nibble (0..15).
 */
void ST_Planar_Draw_VLine_Fast(
	uint16_t *planar_root,
	short planar_row_words,
	short x_abs,
	short y1_abs,
	short y2_abs,
	uint16_t color4);

#endif
