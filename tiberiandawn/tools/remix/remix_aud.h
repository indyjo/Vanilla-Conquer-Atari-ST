#ifndef REMIX_AUD_H
#define REMIX_AUD_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum {
	REMIX_AUD_HDR_LEN = 12,
	REMIX_AUD99_FRAME_MAGIC = 0x0000DEAFUL,
	REMIX_AUD_COMP_PCM = 0,
	REMIX_AUD_COMP_WESTWOOD = 1,
	REMIX_AUD_COMP_IMA99 = 99,
	REMIX_AUD_FLAG_STEREO = 1,
	REMIX_AUD_FLAG_16BIT = 2,
	REMIX_AUD99_MAX_COMPRESSED_PAYLOAD = 16UL * 1024UL * 1024UL,
	REMIX_AUD99_MAX_DECODED_PCM_BYTES = 32UL * 1024UL * 1024UL,
	REMIX_AUD99_MAX_SINGLE_FRAME_PCM = 262144u,
	REMIX_IMA_SKIP_SCRATCH = 256,
	REMIX_AUDIO_PULL_BLOCK = 2048 /* stereo IMA scratch; mono convert uses frame size */
};

typedef size_t (*RemixAudReadFn)(void *ctx, unsigned char *dst, size_t max_len);

typedef struct RemixImaCtx RemixImaCtx;

RemixImaCtx *remix_ima_stream_create(
    const unsigned char *aud, size_t aud_len, unsigned long payload_len,
    RemixAudReadFn read_fn, void *read_ctx);
unsigned long remix_ima_stream_total_samples(const RemixImaCtx *ctx);
unsigned remix_ima_stream_pending_frame_samples(RemixImaCtx *ctx);
unsigned remix_ima_stream_pull_s8(RemixImaCtx *ctx, signed char *dst, unsigned max_out);
void remix_ima_stream_destroy(RemixImaCtx *ctx);

int remix_is_aud99(const unsigned char *in, size_t in_len);
int remix_convert_aud99(const unsigned char *in, size_t in_len, unsigned char **out_buf, size_t *out_len);

#endif /* REMIX_AUD_H */
