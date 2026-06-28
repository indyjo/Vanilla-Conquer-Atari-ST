/*
 * st16_convert.h - In-place standard ICN -> ST16 conversion
 */

#ifndef ST16_CONVERT_H
#define ST16_CONVERT_H

#include "function.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Convert a writable standard 8bpp iconset buffer to ST16 in place.
 * scratch_chunky must hold at least Width*Height bytes (up to ST16_CHUNKY_ICON_MAX_BYTES).
 * Returns FALSE on error; logs start, one line per icon, and completion.
 */
BOOL ST16_Convert_InPlace(uint8_t *base, size_t size, uint8_t *scratch_chunky);

/*
 * TRUE for standard unmasked 8bpp iconsets the runtime will auto-convert.
 * Masked iconsets (TransFlag) are also converted (planar + 1bpp mask).
 */
BOOL ST16_Iconset_Should_Convert(const uint8_t *base, size_t blob_size);

/*
 * Return icondata for ST16 drawing. Converts standard ICN blobs in the MIX buffer on
 * first use (ST16_Convert_InPlace); already-native blobs are unchanged.
 * scratch_chunky may be NULL to use the shared conversion scratch buffer.
 */
const void *ST16_Iconset_Resolve(const void *icondata, uint8_t *scratch_chunky);

/*
 * Allocate the shared chunky conversion scratch (once). Required before convert/prewarm.
 */
BOOL ST16_Ensure_Chunky_Scratch(void);

/*
 * Convert one iconset to ST16 now if needed (no-op when already native or not convertible).
 * Call after theater C2P weights are loaded.
 */
void ST16_Prewarm_Iconset(const void *icondata);

#ifdef __cplusplus
}
#endif

#endif /* ST16_CONVERT_H */
