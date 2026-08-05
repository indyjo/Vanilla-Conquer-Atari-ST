#ifndef ATARILIB_AUDX_POOL_FILE_H_
#define ATARILIB_AUDX_POOL_FILE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Read up to `size` bytes from pool%04x.bin at file offset `begin` into `dst`.
 * Short reads at EOF succeed with the remainder zero-filled.
 * Returns 1 on success, 0 on failure (including a zero-byte read past EOF).
 * Keeps a few pool files open between calls (see AUDX_Pool_Close_All).
 */
int AUDX_Pool_Read(uint16_t pool_id, uint32_t begin, uint32_t size, void *dst);

/** Close any pooled pool%04x.bin handles. Safe if none are open. */
void AUDX_Pool_Close_All(void);

#ifdef __cplusplus
}
#endif

#endif /* ATARILIB_AUDX_POOL_FILE_H_ */
