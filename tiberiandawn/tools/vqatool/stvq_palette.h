/*
 * stvq_palette.h - W16 / hist sidecars for STVQ encode.
 */
#ifndef STVQ_PALETTE_H
#define STVQ_PALETTE_H

#include "vqa_decode.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STVQ_W16_MAGIC "W16"
#define STVQ_W16_BYTES 4116

typedef struct StvqWeightSet {
	char magic[4];
	uint8_t subset[16];
	uint8_t weights[256][16];
} StvqWeightSet;

typedef struct StvqSegPalette {
	StvqWeightSet w16;
	uint16_t stpl[16]; /* host-endian STE color words */
	uint8_t pen_vga6[48]; /* pens 0..15 × RGB VGA6 of W16 subset */
	int have_w16;
} StvqSegPalette;

int stvq_sidecar_paths(const char *vqa_path, int seg, char *pal, char *hist, char *w16, size_t n);

/* CRC form: {w16_dir}/video/{crc:08x}.{seg}.w16 (w16_dir may be NULL → ".") */
int stvq_crc_w16_path(const char *w16_dir, uint32_t crc, int seg, char *w16, size_t n);

int stvq_write_pal(const char *path, const unsigned char pal[768]);
int stvq_write_hist(const char *path, const uint64_t counts[256]);
int stvq_load_w16(const char *path, StvqWeightSet *out);
int stvq_build_stpl(const StvqWeightSet *w16, const unsigned char pal[768], uint16_t stpl_be[16]);

/* Load existing name.<N>.w16; error if missing. */
int stvq_load_segment_w16(const char *vqa_path, int seg, const VqaPalSegment *seginfo, StvqSegPalette *out);

/* Load {w16_dir}/video/{crc:08x}.{seg}.w16; error if missing. */
int stvq_load_segment_w16_crc(
    const char *w16_dir, uint32_t crc, int seg, const VqaPalSegment *seginfo, StvqSegPalette *out);

#ifdef __cplusplus
}
#endif

#endif /* STVQ_PALETTE_H */
