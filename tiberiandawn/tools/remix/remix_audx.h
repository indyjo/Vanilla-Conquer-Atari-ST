#ifndef REMIX_AUDX_H
#define REMIX_AUDX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REMIX_AUDX_PREFIX_SIZE 28u
#define REMIX_AUDX_MAGIC_BE 0x41554458u /* 'AUDX' as BE longword bytes 41 55 44 58 */

uint16_t remix_audx_default_pool_id(const char *mix_basename);
int remix_audx_is_eligible(const char *mix_basename);
int remix_audx_format_pool_name(uint16_t pool_id, char *out, size_t out_cap);

/** Non-zero if this SCORES entry must be dropped from the output MIX (no body, no pool). */
int remix_audx_should_omit_entry(const char *mix_basename, uint32_t crc);

typedef struct RemixAudxPool {
	uint8_t *data;
	size_t size;
	size_t cap;
	uint16_t pool_id;
} RemixAudxPool;

void remix_audx_pool_init(RemixAudxPool *pool, uint16_t pool_id);
void remix_audx_pool_free(RemixAudxPool *pool);

/**
 * Convert classic LE PCM AUD to AUDX meta; append payload to pool.
 * Returns 1 on success (*out_buf / *out_len set, caller frees), 0 on error, -1 if not PCM AUD.
 */
int remix_audx_convert(
    const unsigned char *in, size_t in_len, unsigned char **out_buf, size_t *out_len, RemixAudxPool *pool);

int remix_audx_write_pool(const RemixAudxPool *pool, const char *out_mix_path);

#ifdef __cplusplus
}
#endif

#endif /* REMIX_AUDX_H */
