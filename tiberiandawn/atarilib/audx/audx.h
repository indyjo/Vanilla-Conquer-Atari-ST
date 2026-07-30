/*
 * audx.h — AUDX external-pool audio metadata (Atari ST).
 *
 * On-disk layout uses 68000-native big-endian integers. After the magic longword
 * check, multi-byte fields are read with native uint16_t/uint32_t access.
 */

#ifndef ATARILIB_AUDX_AUDX_H_
#define ATARILIB_AUDX_AUDX_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDX_MAGIC_NATIVE 0x41554458u /* 'AUDX' longword on 68000 */
#define AUDX_PREFIX_SIZE 28u

#define AUDX_PAGE_SIZE 1024u
#define AUDX_PAGE_CACHE_SHARDS 32u
#define AUDX_PAGE_CACHE_SHARD_SIZE 6u /* 32×6×1024 = 192 KiB slab */
#define AUDX_PAGE_CACHE_MAX 65536u /* payload size threshold: <= cache, > file */

#define AUDX_POOL_ID_SOUNDS 0x0005u
#define AUDX_POOL_ID_SPEECH 0x0006u
#define AUDX_POOL_ID_SCORES 0x0007u

typedef struct AudxPrefix {
	uint32_t magic;
	uint16_t rate;
	uint8_t flags;
	uint8_t compression;
	uint32_t size;            /* logical payload bytes (AUD semantics) */
	uint32_t uncomp;
	uint16_t pool_id;
	uint16_t reserved;        /* 0 */
	uint32_t pool_data_begin; /* even offset in pool%04x.bin */
	uint32_t pool_data_size;  /* bytes to read; >= size, even */
} AudxPrefix;

/** Prefix view; only valid after AUDX_Is_Meta. */
static inline AudxPrefix const *AUDX_As_Prefix(void const *meta)
{
	return (AudxPrefix const *)meta;
}

/** TRUE when meta points at an AUDX blob (native magic longword). */
static inline int AUDX_Is_Meta(void const *meta)
{
	if (!meta)
		return 0;
	return AUDX_As_Prefix(meta)->magic == AUDX_MAGIC_NATIVE;
}

/** Format sidecar name pool%04x.bin into out. Returns 1 ok, 0 fail. */
int AUDX_Format_Pool_Name(uint16_t pool_id, char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* ATARILIB_AUDX_AUDX_H_ */
