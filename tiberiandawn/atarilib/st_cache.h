/*
 * st_cache.h — 68030/68040/68060 D-cache maintenance for BLiTTER/CPU coherency.
 *
 * Set ST_BLIT_CACHE_COHERENCY to 0 to disable (default: 1).
 * Call ST_Cache_Init() once at startup; pointers default to no-ops and are
 * rebound when the TOS _CPU cookie (Getcookie(C__CPU)) is 30/40/60.
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

typedef void (*ST_Cache_Range_Fn)(const void *start, size_t len);

/* Push (writeback + invalidate) every touched 16-byte line in [start, start+len). */
extern ST_Cache_Range_Fn ST_Cache_Push_Range;

/* Invalidate without writeback — drop stale/dirty lines after BLiTTER wrote RAM. */
extern ST_Cache_Range_Fn ST_Cache_Invalidate_Range;

void ST_Cache_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* ST_CACHE_H */
