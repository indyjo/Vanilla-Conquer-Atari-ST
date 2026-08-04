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
 * scratch_chunky may be NULL; then conversion uses a small stack scratch for
 * 24×24 tiles only (no heap). Larger unconverted iconsets will not convert here.
 */
const void *ST16_Iconset_Resolve(const void *icondata, uint8_t *scratch_chunky);

/*
 * Snapshot TRANS.ICN from the cached MIX buffer before any ST16 convert.
 * Holds a ~2 KiB copy for theater-change restore. Call once from One_Time.
 */
BOOL ST16_Trans_Iconset_Capture(const void *icondata);

/*
 * Restore TRANS.ICN to its pre-convert bytes and convert in place with the
 * currently installed theater W16. Uses stack scratch only (24×24).
 * Call from Init_Theater after C2P_Load_WeightSet.
 */
void ST16_Trans_Iconset_Restore_And_Convert(void *icondata);

/*
 * Convert one iconset to ST16 now if needed (no-op when already native or not convertible).
 * Call after theater C2P weights are loaded. Uses stack scratch for 24×24 only.
 * Prefer ST16_Trans_Iconset_Restore_And_Convert for TRANS.ICN across theater changes.
 */
void ST16_Prewarm_Iconset(const void *icondata);

#ifdef __cplusplus
}
#endif

#endif /* ST16_CONVERT_H */
