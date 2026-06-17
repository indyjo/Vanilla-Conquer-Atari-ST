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
 * Inclusive axis-aligned rectangle fill with a flat ST nibble (0..15).
 * Same 16-pixel masking and middle packing as ST_Planar_Draw_HLine_Fast, but mask/fill
 * work is done once per rectangle instead of once per scanline.
 */
void ST_Planar_Fill_Rect_Fast(
	uint16_t *planar_root,
	short planar_row_words,
	short y1_abs,
	short y2_abs,
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

/* Fill words for flat nibble color (same table as ST_Planar_Fill_Rect_Fast). */
void ST_Planar_Get_Fill_Words(uint16_t color4, uint16_t out[4]);

#endif
