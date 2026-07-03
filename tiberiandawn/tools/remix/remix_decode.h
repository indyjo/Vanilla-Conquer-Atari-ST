#ifndef REMIX_DECODE_H
#define REMIX_DECODE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Bounded LCW decompress for remix / remix-web.
 * Returns bytes written to dest, or -1 on corrupt input / buffer overrun.
 */
int remix_lcw_uncompress(
    const void *source, size_t source_len, void *dest, unsigned dest_len);

/**
 * Bounded XOR delta apply for remix / remix-web.
 * Returns 1 on success (including normal terminator), 0 on corrupt/truncated input.
 */
int remix_xor_delta_apply(void *dst, size_t dst_len, const void *src, size_t src_len);

#ifdef __cplusplus
}
#endif

#endif /* REMIX_DECODE_H */
