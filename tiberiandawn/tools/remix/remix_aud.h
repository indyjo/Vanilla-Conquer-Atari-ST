#ifndef REMIX_AUD_H
#define REMIX_AUD_H

#include <stddef.h>

enum {
	REMIX_AUD_HDR_LEN = 12,
	REMIX_AUD99_FRAME_MAGIC = 0x0000DEAFUL,
	REMIX_AUD_COMP_PCM = 0,
	REMIX_AUD_COMP_IMA99 = 99,
	REMIX_AUD_FLAG_STEREO = 1,
	REMIX_AUD_FLAG_16BIT = 2,
	REMIX_AUD_FLAG_DUP2X = 4,
	REMIX_AUD99_MAX_COMPRESSED_PAYLOAD = 16UL * 1024UL * 1024UL, /* host tool; STE runtime uses 2 MiB */
	REMIX_AUD99_MAX_DECODED_PCM_BYTES = 32UL * 1024UL * 1024UL,
	REMIX_AUD99_MAX_SINGLE_FRAME_PCM = 262144u,
	REMIX_IMA_SKIP_SCRATCH = 256,
	REMIX_AUDIO_PULL_BLOCK = 512
};

/*
 * Convert an AUD99 (Westwood IMA) asset to 8-bit mono PCM suitable for the
 * Atari STE audio path (11 kHz header rate, STE_AUD_FLAG_DUP2X playback hint).
 *
 * On success, *out_buf receives a newly allocated buffer and *out_len its size.
 * Returns 1 if conversion was performed, 0 if the input is not AUD99 or cannot
 * be converted (caller should pass the original bytes through unchanged).
 */
int remix_is_aud99(const unsigned char *in, size_t in_len);
int remix_convert_aud99(const unsigned char *in, size_t in_len, unsigned char **out_buf, size_t *out_len);

#endif /* REMIX_AUD_H */
