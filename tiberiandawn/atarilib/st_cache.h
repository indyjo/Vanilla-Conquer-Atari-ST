/*
 * st_cache.h — 68030/68040/68060 D-cache maintenance for BLiTTER/CPU coherency.
 *
 * Set ST_BLIT_CACHE_COHERENCY to 0 to disable (default: 1).
 * Case B (CPU dirty cache, BLiTTER writes RAM): push before blit, invalidate after.
 */

#ifndef ST_CACHE_H
#define ST_CACHE_H

#include <stddef.h>

#ifndef ST_BLIT_CACHE_COHERENCY
#define ST_BLIT_CACHE_COHERENCY 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Push (writeback + invalidate) every touched 16-byte line in [start, start+len). */
void ST_Cache_Push_Range(const void *start, size_t len);

/* Invalidate without writeback — drop stale/dirty lines after BLiTTER wrote RAM. */
void ST_Cache_Invalidate_Range(const void *start, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ST_CACHE_H */
