#ifndef ATARILIB_AUDX_PAGE_CACHE_H_
#define ATARILIB_AUDX_PAGE_CACHE_H_

#include "audx.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Allocate RankCache shards + 192 KiB page slab.
 * Returns 0 on success, -1 on failure. Idempotent if already inited.
 */
int AUDX_Page_Cache_Init(void);

/** Free slab and shards. No-op if not inited. */
void AUDX_Page_Cache_Shutdown(void);

/** Non-zero when Init succeeded and Shutdown has not run. */
int AUDX_Page_Cache_Is_Inited(void);

/**
 * Return a pointer to the 1024-byte page covering `file_offset` in pool_id,
 * or NULL on I/O / init failure. Pointer is valid until the next miss that
 * retargets that slot (or Shutdown).
 */
uint8_t const *AUDX_Page_Get(uint16_t pool_id, uint32_t file_offset);

#ifdef __cplusplus
}
#endif

#endif /* ATARILIB_AUDX_PAGE_CACHE_H_ */
