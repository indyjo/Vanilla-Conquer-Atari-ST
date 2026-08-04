#ifndef ATARILIB_AUDX_PAGE_CACHE_H_
#define ATARILIB_AUDX_PAGE_CACHE_H_

#include "audx.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Allocate RankCache shards + cache/stream page slabs.
 * Returns 0 on success, -1 on failure. Idempotent if already inited.
 */
int AUDX_Page_Cache_Init(void);

/** Free slab and shards. No-op if not inited. */
void AUDX_Page_Cache_Shutdown(void);

/** Non-zero when Init succeeded and Shutdown has not run. */
int AUDX_Page_Cache_Is_Inited(void);

/**
 * Return a pointer to the 1024-byte page covering `file_offset` in pool_id
 * (page base = file_offset / AUDX_PAGE_SIZE), or NULL on I/O / init / pin-stuck
 * failure. Pointer is valid while pinned, or until an unpinned miss retargets
 * that RankCache slot (or Shutdown).
 *
 * May call GEMDOS via AUDX_Pool_Read — main thread only (Play_Sample /
 * Sound_Callback), never from VBL.
 */
uint8_t const *AUDX_Page_Get(uint16_t pool_id, uint32_t file_offset);

/**
 * Pin/unpin a slab pointer from AUDX_Page_Get or AUDX_Stream_Page_Acquire.
 * Main thread only. Pinned RankCache pages are never retargeted.
 */
void AUDX_Page_Pin(uint8_t const *page);
void AUDX_Page_Unpin(uint8_t const *page);

/**
 * Acquire a free reserved stream slab (not in RankCache) for large-file fills.
 * Returns NULL if all stream slabs are pinned. Caller fills via AUDX_Pool_Read,
 * then AUDX_Page_Pin before publishing to a page ring.
 */
uint8_t *AUDX_Stream_Page_Acquire(void);

#ifdef __cplusplus
}
#endif

#endif /* ATARILIB_AUDX_PAGE_CACHE_H_ */
