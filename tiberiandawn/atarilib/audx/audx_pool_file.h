#ifndef ATARILIB_AUDX_POOL_FILE_H_
#define ATARILIB_AUDX_POOL_FILE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Read `size` bytes from pool%04x.bin at file offset `begin` into `dst`.
 * Returns 1 on success, 0 on failure.
 */
int AUDX_Pool_Read(uint16_t pool_id, uint32_t begin, uint32_t size, void *dst);

#ifdef __cplusplus
}
#endif

#endif /* ATARILIB_AUDX_POOL_FILE_H_ */
