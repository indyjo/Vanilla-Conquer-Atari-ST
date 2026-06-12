#ifndef REMIX_AUDIO_H
#define REMIX_AUDIO_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "remix_aud.h"

int remix_aud_is_target(const unsigned char *hdr, size_t hdr_len);
int remix_aud_needs_convert(const unsigned char *hdr, size_t hdr_len, uint32_t file_size);

typedef size_t (*RemixPayloadReadFn)(void *ctx, unsigned char *dst, size_t max_len);

typedef void (*RemixProgressFn)(void *ctx, const char *verb, unsigned done, unsigned total);

/*
 * Convert an embedded .AUD payload to 11025 Hz 8-bit mono PCM.
 * Returns 1 on success, 0 on failure (caller may fall back to raw copy).
 */
int remix_audio_stream_convert(
    const unsigned char *probe, size_t probe_len, uint32_t file_size,
    RemixPayloadReadFn read_fn, void *read_ctx, long payload_start, FILE *in,
    FILE *out, RemixProgressFn progress, void *progress_ctx, uint32_t *out_bytes);

#endif /* REMIX_AUDIO_H */
