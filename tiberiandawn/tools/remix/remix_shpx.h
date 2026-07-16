#ifndef REMIX_SHPX_H
#define REMIX_SHPX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REMIX_SHPX_POOL_ID_DEFAULT 0x0001u

/**
 * Default pool_id for an eligible MIX basename (case-insensitive):
 * CONQUER=1, TEMPERAT=2, DESERT=3, WINTER=4. Returns 0 if not eligible.
 */
uint16_t remix_shpx_default_pool_id(const char *mix_basename);

/** TRUE when basename is eligible for SHPX KeyFrame conversion. */
int remix_shpx_is_eligible(const char *mix_basename);

/** Format sidecar name pool%04x.bin into out (cap bytes). Returns 1 ok, 0 fail. */
int remix_shpx_format_pool_name(uint16_t pool_id, char *out, size_t out_cap);

/** TRUE for monolithic KeyFrame SHP (not ShapeBlock / SHPX). */
int remix_is_keyframe_shp(const unsigned char *data, size_t len);

typedef struct RemixShpxPool {
	uint8_t *data;
	size_t size;
	size_t cap;
	uint16_t pool_id;
} RemixShpxPool;

typedef struct RemixShpxConvertOpts {
	int verbose;
	uint32_t entry_crc;
} RemixShpxConvertOpts;

void remix_shpx_pool_init(RemixShpxPool *pool, uint16_t pool_id);
void remix_shpx_pool_free(RemixShpxPool *pool);

/**
 * Convert monolithic KeyFrame SHP to SHPX metadata; append payload tail to pool.
 * Returns 1 on success (*out_buf / *out_len set), 0 on error, -1 if not applicable.
 */
int remix_shpx_convert(
    const unsigned char *in, size_t in_len, unsigned char **out_buf, size_t *out_len,
    RemixShpxPool *pool, const RemixShpxConvertOpts *opts);

/** Write accumulated pool beside out_mix_path. Returns 1 ok, 0 fail. */
int remix_shpx_write_pool(const RemixShpxPool *pool, const char *out_mix_path);

#ifdef __cplusplus
}
#endif

#endif /* REMIX_SHPX_H */
