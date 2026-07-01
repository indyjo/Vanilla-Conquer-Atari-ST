/*
 * shpx.h — SHPX external-pool KeyFrame metadata (Atari ST runtime).
 *
 * On-disk layout uses 68000-native big-endian integers. After the magic longword
 * check, multi-byte fields are read with native uint16_t/uint32_t access.
 */

#ifndef ATARILIB_SHPX_H_
#define ATARILIB_SHPX_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHPX_MAGIC_NATIVE 0x53485058u /* 'SHPX' longword on 68000 */
#define SHPX_PREFIX_SIZE 38u
#define SHPX_POOL_SLICE_MAX 65536u /* static pool read buffer for SHPX slice I/O */

typedef struct ShpxKfHeader {
	uint16_t frames;
	uint16_t x;
	uint16_t y;
	uint16_t width;
	uint16_t height;
	uint16_t largest_frame_size;
	int16_t flags;
} ShpxKfHeader;

typedef struct ShpxClipEntry {
	uint16_t x;
	uint16_t y;
	uint16_t w;
	uint16_t h;
} ShpxClipEntry;

typedef struct ShpxPrefix {
	uint32_t magic;
	ShpxKfHeader kf;
	uint16_t pool_id;
	uint16_t reserved;
	uint32_t frame_table_offset;
	uint32_t clip_table_offset;
	uint32_t pool_data_begin;
	uint32_t pool_data_size;
} ShpxPrefix;

/** Prefix view; only valid after SHPX_Is_Meta. */
static inline ShpxPrefix const *SHPX_As_Prefix(void const *meta)
{
	return (ShpxPrefix const *)meta;
}

/** TRUE when meta points at an SHPX blob (native magic longword). */
static inline int SHPX_Is_Meta(void const *meta)
{
	if (!meta)
		return 0;
	return SHPX_As_Prefix(meta)->magic == SHPX_MAGIC_NATIVE;
}

/**
 * Load `size` bytes from pool%04x.bin at file offset `begin` into a global 64 KiB buffer.
 * Returns a pointer to the first byte on success, NULL on error.
 * Re-reads from disk only when (pool_id, begin, size) differs from the last successful call.
 */
void *SHPX_Pool_Read_Slice(uint16_t pool_id, uint32_t begin, uint32_t size);

/** Clip for frame `frame` from the SHPX clip table. Returns 0 if unavailable. */
int SHPX_Get_Frame_Clip(void const *meta, unsigned frame, uint16_t *cx, uint16_t *cy, uint16_t *cw,
    uint16_t *ch);

#ifdef __cplusplus
}
#endif

#endif /* ATARILIB_SHPX_H_ */
