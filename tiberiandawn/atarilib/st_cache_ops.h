/*
 * st_cache_ops.h — 68030+ D-cache line ops (cpushl/cinvl); separate TU for -mcpu=68040.
 */

#ifndef ST_CACHE_OPS_H
#define ST_CACHE_OPS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void ST_Cache_Push_Lines(const void *start, size_t len);
void ST_Cache_Invalidate_Lines(const void *start, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ST_CACHE_OPS_H */
