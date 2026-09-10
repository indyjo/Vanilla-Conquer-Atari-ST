/*
 * page_region_cache.h — Contiguous SHPX pool-slice cache (Atari ST).
 *
 * 168 KiB slab, largest-first: [48 KiB × 2][24 KiB × 2][8 KiB × 3].
 * One RankCache per size tier. Requests larger than 48 KiB steal the T48
 * span (both slots) and whole following tiers as needed. Retargeting a
 * stolen tier invalidates all larger tiers and returns to normal mode.
 */

#ifndef ATARILIB_PAGE_REGION_CACHE_H_
#define ATARILIB_PAGE_REGION_CACHE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAGE_REGION_CACHE_BYTES (168u * 1024u)

typedef int (*Page_Region_Cache_Fill)(void *ctx, uint16_t pool_id, uint32_t begin, uint32_t size,
    void *dst);

/** Alloc 168 KiB slab and RankCaches. Idempotent. */
void Page_Region_Cache_Init(void);

/**
 * Return a pointer to `size` cached bytes for (pool_id, begin), or NULL.
 * Pointer is valid until a later Get retargets that slot (or a steal/restore
 * that covers it). fill is invoked on miss; 0 means success.
 */
void *Page_Region_Cache_Get(uint16_t pool_id, uint32_t begin, uint32_t size,
    Page_Region_Cache_Fill fill, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ATARILIB_PAGE_REGION_CACHE_H_ */
