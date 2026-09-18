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
	int16_t *pcm16;        /* SND muxed with this VQFR; may be NULL */
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
	unsigned sample_rate;
	unsigned channels;
	unsigned bits_per_sample;
} VqaDecode;

typedef struct VqaStream VqaStream;

int vqa_stream_open(const char *path, VqaStream **out);
void vqa_stream_close(VqaStream *s);
const VqaHeader *vqa_stream_header(const VqaStream *s);
unsigned vqa_stream_width(const VqaStream *s);
unsigned vqa_stream_height(const VqaStream *s);
unsigned vqa_stream_blocks_w(const VqaStream *s);
unsigned vqa_stream_blocks_h(const VqaStream *s);
unsigned vqa_stream_frame_count(const VqaStream *s);
unsigned vqa_stream_sample_rate(const VqaStream *s);
const VqaPalSegment *vqa_stream_segments(const VqaStream *s, unsigned *count);
/* 1 = frame in *fr (caller frees with vqa_decoded_frame_clear), 0 = done, -1 = error */
int vqa_stream_next(VqaStream *s, VqaDecodedFrame *fr);
void vqa_decoded_frame_clear(VqaDecodedFrame *fr);

int vqa_decode_file(const char *path, VqaDecode *out);
void vqa_decode_free(VqaDecode *dec);

#ifdef __cplusplus
}
#endif

#endif /* VQA_DECODE_H */
