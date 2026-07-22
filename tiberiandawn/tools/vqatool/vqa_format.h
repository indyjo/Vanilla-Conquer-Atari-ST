/*
 * vqa_format.h - Westwood VQA IFF chunk IDs and on-disk header (host tool).
 */
#ifndef VQA_FORMAT_H
#define VQA_FORMAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VQA_MAKE_CHUNK(a, b, c, d) ((uint32_t)(((uint32_t)(a)) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24)))

#define VQA_CHUNK_FORM VQA_MAKE_CHUNK('F', 'O', 'R', 'M')
#define VQA_CHUNK_WVQA VQA_MAKE_CHUNK('W', 'V', 'Q', 'A')
#define VQA_CHUNK_VQHD VQA_MAKE_CHUNK('V', 'Q', 'H', 'D')
#define VQA_CHUNK_NAME VQA_MAKE_CHUNK('N', 'A', 'M', 'E')
#define VQA_CHUNK_FINF VQA_MAKE_CHUNK('F', 'I', 'N', 'F')
#define VQA_CHUNK_VQFR VQA_MAKE_CHUNK('V', 'Q', 'F', 'R')
#define VQA_CHUNK_VQFL VQA_MAKE_CHUNK('V', 'Q', 'F', 'L')
#define VQA_CHUNK_VQFK VQA_MAKE_CHUNK('V', 'Q', 'F', 'K')
#define VQA_CHUNK_CBF0 VQA_MAKE_CHUNK('C', 'B', 'F', '0')
#define VQA_CHUNK_CBFZ VQA_MAKE_CHUNK('C', 'B', 'F', 'Z')
#define VQA_CHUNK_CBP0 VQA_MAKE_CHUNK('C', 'B', 'P', '0')
#define VQA_CHUNK_CBPZ VQA_MAKE_CHUNK('C', 'B', 'P', 'Z')
#define VQA_CHUNK_CPL0 VQA_MAKE_CHUNK('C', 'P', 'L', '0')
#define VQA_CHUNK_CPLZ VQA_MAKE_CHUNK('C', 'P', 'L', 'Z')
#define VQA_CHUNK_VPT0 VQA_MAKE_CHUNK('V', 'P', 'T', '0')
#define VQA_CHUNK_VPTZ VQA_MAKE_CHUNK('V', 'P', 'T', 'Z')
#define VQA_CHUNK_VPTR VQA_MAKE_CHUNK('V', 'P', 'T', 'R')
#define VQA_CHUNK_VPTD VQA_MAKE_CHUNK('V', 'P', 'T', 'D')
#define VQA_CHUNK_VPTK VQA_MAKE_CHUNK('V', 'P', 'T', 'K')
#define VQA_CHUNK_VPRZ VQA_MAKE_CHUNK('V', 'P', 'R', 'Z')
#define VQA_CHUNK_VPDZ VQA_MAKE_CHUNK('V', 'P', 'D', 'Z')
#define VQA_CHUNK_VPKZ VQA_MAKE_CHUNK('V', 'P', 'K', 'Z')
#define VQA_CHUNK_SND0 VQA_MAKE_CHUNK('S', 'N', 'D', '0')
#define VQA_CHUNK_SND1 VQA_MAKE_CHUNK('S', 'N', 'D', '1')
#define VQA_CHUNK_SND2 VQA_MAKE_CHUNK('S', 'N', 'D', '2')
#define VQA_CHUNK_SN2J VQA_MAKE_CHUNK('S', 'N', '2', 'J')
#define VQA_CHUNK_SNA0 VQA_MAKE_CHUNK('S', 'N', 'A', '0')
#define VQA_CHUNK_SNA1 VQA_MAKE_CHUNK('S', 'N', 'A', '1')
#define VQA_CHUNK_SNA2 VQA_MAKE_CHUNK('S', 'N', 'A', '2')

#define VQA_VQHD_SIZE 42u
#define VQA_PALETTE_BYTES 768u
#define VQA_LCW_PAL_WORK 1792u

#pragma pack(push, 1)
typedef struct VqaHeader {
	uint16_t version;
	uint16_t flags;
	uint16_t frames;
	uint16_t image_width;
	uint16_t image_height;
	uint8_t block_width;
	uint8_t block_height;
	uint8_t fps;
	uint8_t groupsize;
	uint16_t num1_colors;
	uint16_t cb_entries;
	uint16_t xpos;
	uint16_t ypos;
	uint16_t max_framesize;
	uint16_t sample_rate;
	uint8_t channels;
	uint8_t bits_per_sample;
	uint16_t alt_sample_rate;
	uint8_t alt_channels;
	uint8_t alt_bits_per_sample;
	uint8_t color_mode;
	uint8_t field_21;
	uint32_t max_compressed_cb_size;
	uint32_t field_26;
} VqaHeader;
#pragma pack(pop)

typedef struct VqaChunkHdr {
	uint32_t id;
	uint32_t size_be;
} VqaChunkHdr;

const char *vqa_chunk_name(uint32_t id);
const char *vqa_color_mode_name(uint8_t mode);
void vqa_format_flags(uint16_t flags, char *buf, unsigned buf_len);

/* VGA DAC is 6-bit; Westwood pals often leave junk in bits 6–7. Mask in place. */
void vqa_sanitize_vga6_palette(unsigned char pal[VQA_PALETTE_BYTES]);

#ifdef __cplusplus
}
#endif

#endif /* VQA_FORMAT_H */
