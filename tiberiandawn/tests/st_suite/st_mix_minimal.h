/*
 * Minimal Westwood .MIX reader for ST test harness (index + embedded blob).
 */
#ifndef ST_MIX_MINIMAL_H
#define ST_MIX_MINIMAL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Reads mix_path, extracts embedded file by name (case-insensitive).
 * On success: *out_data is malloc'd file bytes, *out_size is length. Caller frees *out_data.
 * Returns 0 on success, negative on error.
 */
int st_mix_extract_file(const char *mix_path, const char *entry_name,
		unsigned char **out_data, size_t *out_size);

/* Human-readable reason for negative st_mix_extract_file return codes. */
const char *st_mix_extract_errmsg(int err);

#ifdef __cplusplus
}
#endif

#endif
