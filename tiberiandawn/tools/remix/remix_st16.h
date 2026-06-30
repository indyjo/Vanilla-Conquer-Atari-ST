#ifndef REMIX_ST16_H
#define REMIX_ST16_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** TRUE for theater terrain MIX basenames (case-insensitive). */
int remix_st16_is_theater_mix(const char *mix_basename);

/** Map theater MIX basename to W16 stem (e.g. TEMPERAT.MIX -> TEMPERAT). Returns 0 if not theater. */
int remix_st16_w16_stem_for_mix(const char *mix_basename, char *stem_out, size_t stem_out_len);

/** Install C2P weights from <stem>.W16 under w16_dir (NULL = cwd). Returns 1 ok, 0 fail. */
int remix_st16_install_weights_from_file(const char *w16_dir, const char *stem);

/** Install C2P weights from an in-memory W16 bundle (4116 bytes). Returns 1 ok, 0 fail. */
int remix_st16_install_weights_from_buffer(const uint8_t *data, size_t len);

/** TRUE when blob is already native ST16 (skip conversion). */
int remix_st16_is_native(const uint8_t *data, size_t len);

/** TRUE when blob is a standard 8bpp iconset eligible for ST16 conversion. */
int remix_st16_should_convert(const uint8_t *data, size_t len);

/*
 * Convert payload buffer in place; *inout_size is updated to the shrunk size.
 * Requires C2P weights installed. Returns 1 ok, 0 fail.
 */
int remix_st16_convert_payload(uint8_t *buf, size_t *inout_size);

/** Install theater W16 for cfg->mix_basename when ST16 conversion is enabled. Returns 1 ok, 0 fail. */
int remix_st16_prepare_theater_mix(int convert_st16_iconsets, const char *mix_basename, const char *w16_dir);

/** Extract uppercase MIX basename from a path into out (cap bytes). */
void remix_path_basename(const char *path, char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* REMIX_ST16_H */
