/*
 * vqa_decode.h - Sequential host decode of Westwood VQA frames.
 */
#ifndef VQA_DECODE_H
#define VQA_DECODE_H

#include "vqa_format.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VqaPalSegment {
	int start_frame;
	int end_frame; /* inclusive */
	unsigned char pal[VQA_PALETTE_BYTES];
	uint32_t hash;
} VqaPalSegment;

typedef struct VqaDecodedFrame {
	int index;
	int segment;
	unsigned char *pixels; /* width*height VGA indices */
	int16_t *pcm16;        /* optional; may be NULL */
	size_t pcm16_count;
	int has_palette_change;
} VqaDecodedFrame;

typedef struct VqaDecode {
	VqaHeader hdr;
	unsigned width;
	unsigned height;
	unsigned blocks_w;
	unsigned blocks_h;
	unsigned frame_count;
	VqaDecodedFrame *frames;
	VqaPalSegment *segments;
	unsigned segment_count;
	int16_t *all_pcm16;
	size_t all_pcm16_count;
	unsigned sample_rate;
	unsigned channels;
	unsigned bits_per_sample;
} VqaDecode;

int vqa_decode_file(const char *path, VqaDecode *out);
void vqa_decode_free(VqaDecode *dec);

#ifdef __cplusplus
}
#endif

#endif /* VQA_DECODE_H */
