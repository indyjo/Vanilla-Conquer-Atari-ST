/*
 * Minimal PCX loader for ST test harness (no CCFileClass / GraphicBufferClass).
 * Loads 8-bit, 1-plane RLE PCX into a tight width*height buffer + 768-byte VGA palette.
 */
#ifndef ST_PCX_MINIMAL_H
#define ST_PCX_MINIMAL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Returns malloc'd buffer (row_stride * h bytes), palette 768 bytes (6-bit RGB as in game).
 * Frees previous *out_buf if non-NULL. Row stride may exceed w when PCX bytes_per_line > width.
 */
int st_pcx_load(const char *path, unsigned char **out_buf, int *out_w, int *out_h, int *out_row_stride,
		unsigned char *palette768);

/*
 * Same as st_pcx_load but PCX file already in memory (e.g. extracted from UPDATE.MIX).
 * file_bytes must include VGA palette tail (769 bytes: 0x0C + 768 RGB).
 */
int st_pcx_load_from_memory(const unsigned char *file_bytes, size_t file_len, unsigned char **out_buf,
		int *out_w, int *out_h, int *out_row_stride, unsigned char *palette768);

#ifdef __cplusplus
}
#endif

#endif
