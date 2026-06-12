#ifndef REMIX_UNZAP_H
#define REMIX_UNZAP_H

#include <stddef.h>

/*
 * Westwood AUD compression type 1 ("Unzap") streaming decode.
 * Invokes write_fn for each decompressed unsigned 8-bit PCM byte.
 * Returns 1 on success, 0 on corrupt input.
 */
typedef int (*RemixUnzapWriteFn)(void *ctx, unsigned char sample);

int remix_unzap_stream(
    const unsigned char *src, size_t src_len, size_t out_len,
    RemixUnzapWriteFn write_fn, void *ctx);

#endif /* REMIX_UNZAP_H */
