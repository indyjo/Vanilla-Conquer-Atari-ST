/*
 * stvq_format.h - FORM 'STVQ' chunk IDs and header (Atari ST runtime).
 */
#ifndef STVQ_FORMAT_H
#define STVQ_FORMAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Big-endian FourCC (IFF on m68k): file bytes a,b,c,d <-> uint32 0xaabbccdd. */
#define STVQ_MAKE_CHUNK(a, b, c, d)                                                                                    \
	((uint32_t)(((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d)))

#define STVQ_CHUNK_FORM STVQ_MAKE_CHUNK('F', 'O', 'R', 'M')
#define STVQ_CHUNK_STVQ STVQ_MAKE_CHUNK('S', 'T', 'V', 'Q')
#define STVQ_CHUNK_STHD STVQ_MAKE_CHUNK('S', 'T', 'H', 'D')
#define STVQ_CHUNK_NAME STVQ_MAKE_CHUNK('N', 'A', 'M', 'E')
#define STVQ_CHUNK_STPL STVQ_MAKE_CHUNK('S', 'T', 'P', 'L')
#define STVQ_CHUNK_STCB STVQ_MAKE_CHUNK('S', 'T', 'C', 'B')
#define STVQ_CHUNK_STCR STVQ_MAKE_CHUNK('S', 'T', 'C', 'R')
#define STVQ_CHUNK_STFI STVQ_MAKE_CHUNK('S', 'T', 'F', 'I')
#define STVQ_CHUNK_STFR STVQ_MAKE_CHUNK('S', 'T', 'F', 'R')
#define STVQ_CHUNK_STVD STVQ_MAKE_CHUNK('S', 'T', 'V', 'D')
#define STVQ_CHUNK_SND0 STVQ_MAKE_CHUNK('S', 'N', 'D', '0')
#define STVQ_CHUNK_STEN STVQ_MAKE_CHUNK('S', 'T', 'E', 'N')

#define STVQ_STHD_SIZE 32u
#define STVQ_STPL_BYTES 32u
#define STVQ_TILE_BYTES 32u
#define STVQ_STCR_ENTRY_BYTES 34u
#define STVQ_VERSION 1u
#define STVQ_SAMPLE_RATE 12517u
#define STVQ_FLAG_SOUND 1u

#define STVQ_SCREEN_W 320u
#define STVQ_SCREEN_H 200u
#define STVQ_SCREEN_PITCH 160u
#define STVQ_SCREEN_BYTES 32000u

typedef struct StvqHeader {
	uint16_t version;
	uint16_t flags;
	uint16_t frames;
	uint16_t width;
	uint16_t height;
	uint8_t block_w;
	uint8_t block_h;
	uint8_t fps;
	uint8_t reserved0;
	uint16_t cb_entries;
	uint16_t sample_rate;
	uint8_t channels;
	uint8_t bits_per_sample;
	uint16_t max_frame_bytes;
	uint16_t reserved1;
	uint32_t reserved2;
} StvqHeader;

/* Native big-endian load (m68k): must be even-aligned -- IFF payloads are. */
static inline uint16_t stvq_read_be16(const unsigned char *p)
{
	return *(const uint16_t *)(const void *)p;
}

static inline uint32_t stvq_read_be32(const unsigned char *p)
{
	return *(const uint32_t *)(const void *)p;
}

void stvq_header_unpack(const unsigned char in[STVQ_STHD_SIZE], StvqHeader *h);
unsigned stvq_tiles_x(unsigned width);
unsigned stvq_tiles_y(unsigned height);
unsigned stvq_iff_padded(uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* STVQ_FORMAT_H */
