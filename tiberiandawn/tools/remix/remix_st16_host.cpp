/*
 * remix ST16 host layer — C2P weight install + ST16_Convert_InPlace wrappers.
 */

#include "remix_st16.h"

#include "c2p.h"
#include "st16_convert.h"
#include "st16_iconset.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int str_ieq(const char *a, const char *b)
{
	if (!a || !b) {
		return 0;
	}
	while (*a && *b) {
		if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) {
			return 0;
		}
		++a;
		++b;
	}
	return *a == *b;
}

void remix_path_basename(const char *path, char *out, size_t out_cap)
{
	const char *base;
	const char *slash;

	if (!out || out_cap == 0) {
		return;
	}
	out[0] = '\0';
	if (!path) {
		return;
	}

	base = path;
	for (slash = path; *slash; ++slash) {
		if (*slash == '/' || *slash == '\\') {
			base = slash + 1;
		}
	}

	snprintf(out, out_cap, "%s", base);
	for (size_t i = 0; out[i]; ++i) {
		out[i] = (char)toupper((unsigned char)out[i]);
	}
}

int remix_st16_is_theater_mix(const char *mix_basename)
{
	if (!mix_basename) {
		return 0;
	}
	return str_ieq(mix_basename, "TEMPERAT.MIX") || str_ieq(mix_basename, "DESERT.MIX")
	    || str_ieq(mix_basename, "WINTER.MIX") || str_ieq(mix_basename, "SNOW.MIX")
	    || str_ieq(mix_basename, "JUNGLE.MIX");
}

int remix_st16_w16_stem_for_mix(const char *mix_basename, char *stem_out, size_t stem_out_len)
{
	if (!stem_out || stem_out_len == 0) {
		return 0;
	}
	stem_out[0] = '\0';

	if (str_ieq(mix_basename, "TEMPERAT.MIX")) {
		snprintf(stem_out, stem_out_len, "TEMPERAT");
		return 1;
	}
	if (str_ieq(mix_basename, "DESERT.MIX")) {
		snprintf(stem_out, stem_out_len, "DESERT");
		return 1;
	}
	if (str_ieq(mix_basename, "WINTER.MIX") || str_ieq(mix_basename, "SNOW.MIX")) {
		snprintf(stem_out, stem_out_len, "WINTER");
		return 1;
	}
	if (str_ieq(mix_basename, "JUNGLE.MIX")) {
		snprintf(stem_out, stem_out_len, "JUNGLE");
		return 1;
	}
	return 0;
}

static void remix_st16_lowercase_stem(const char *stem, char *out, size_t out_cap)
{
	size_t i;

	if (!out || out_cap == 0) {
		return;
	}
	out[0] = '\0';
	if (!stem) {
		return;
	}
	for (i = 0; stem[i] && i + 1 < out_cap; ++i) {
		out[i] = (char)tolower((unsigned char)stem[i]);
	}
	out[i] = '\0';
}

static int remix_st16_build_w16_path(
    char *path, size_t path_cap, const char *w16_dir, const char *stem, int lower_stem, int lower_ext)
{
	const char *ext = lower_ext ? ".w16" : ".W16";
	char stem_lower[64];
	const char *use_stem;

	if (!path || path_cap == 0 || !stem || !stem[0]) {
		return 0;
	}
	if (lower_stem) {
		remix_st16_lowercase_stem(stem, stem_lower, sizeof(stem_lower));
		if (!stem_lower[0]) {
			return 0;
		}
		use_stem = stem_lower;
	} else {
		use_stem = stem;
	}
	if (w16_dir && w16_dir[0]) {
		size_t dir_len = strlen(w16_dir);
		int need_sep = dir_len > 0 && w16_dir[dir_len - 1] != '/';
		if (need_sep) {
			return snprintf(path, path_cap, "%s/%s%s", w16_dir, use_stem, ext) < (int)path_cap;
		}
		return snprintf(path, path_cap, "%s%s%s", w16_dir, use_stem, ext) < (int)path_cap;
	}
	return snprintf(path, path_cap, "%s%s", use_stem, ext) < (int)path_cap;
}

static FILE *remix_st16_fopen_w16(const char *w16_dir, const char *stem, char *path_out, size_t path_out_cap)
{
	char path[512];
	FILE *f;
	static const struct {
		int lower_stem;
		int lower_ext;
	} variants[] = {
	    {1, 1}, /* desert.w16 — release zip / atari-assets convention */
	    {0, 0}, /* DESERT.W16 */
	    {1, 0}, /* desert.W16 */
	    {0, 1}, /* DESERT.w16 */
	};

	for (size_t i = 0; i < sizeof(variants) / sizeof(variants[0]); ++i) {
		if (!remix_st16_build_w16_path(
			path, sizeof(path), w16_dir, stem, variants[i].lower_stem, variants[i].lower_ext)) {
			continue;
		}
		f = fopen(path, "rb");
		if (f) {
			if (path_out && path_out_cap > 0) {
				snprintf(path_out, path_out_cap, "%s", path);
			}
			return f;
		}
	}
	if (path_out && path_out_cap > 0) {
		remix_st16_build_w16_path(path_out, path_out_cap, w16_dir, stem, 1, 1);
	}
	return NULL;
}

int remix_st16_install_weights_from_buffer(const uint8_t *data, size_t len)
{
	const C2P_WeightSet *ws;

	if (!data || len != (size_t)C2P_WEIGHTSET_FILE_BYTES) {
		return 0;
	}
	ws = (const C2P_WeightSet *)data;
	return C2P_Install_WeightSet(ws) ? 1 : 0;
}

int remix_st16_install_weights_from_file(const char *w16_dir, const char *stem)
{
	char path[512];
	FILE *f;
	C2P_WeightSet weights;
	size_t got;

	if (!stem || !stem[0]) {
		return 0;
	}

	f = remix_st16_fopen_w16(w16_dir, stem, path, sizeof(path));
	if (!f) {
		fprintf(stderr, "remix: ST16: missing C2P weights: %s\n", path);
		return 0;
	}
	got = fread(&weights, 1, sizeof(weights), f);
	fclose(f);
	if (got != sizeof(weights)) {
		fprintf(stderr, "remix: ST16: short read: %s\n", path);
		return 0;
	}
	if (!C2P_Install_WeightSet(&weights)) {
		fprintf(stderr, "remix: ST16: invalid C2P weights: %s\n", path);
		return 0;
	}
	return 1;
}

int remix_st16_is_native(const uint8_t *data, size_t len)
{
	uint32_t icons_off;
	uint32_t blob_size;

	if (!data || len < ST16_ICONTROL_SIZE + ST16_CHUNK_TOTAL) {
		return 0;
	}

	/* After offline convert, numeric header/chunk fields are BE; chunk magic bytes are not swapped. */
	if (data[ST16_CHUNK_OFFSET + 0] != 'S' || data[ST16_CHUNK_OFFSET + 1] != 'T'
	    || data[ST16_CHUNK_OFFSET + 2] != '1' || data[ST16_CHUNK_OFFSET + 3] != '6') {
		return 0;
	}
	if (ST16_Read_BE32(data + ST16_CHUNK_OFFSET + 4) != ST16_PAYLOAD_SIZE) {
		return 0;
	}
	if (ST16_Read_BE16(data + ST16_CHUNK_OFFSET + 10) != 0) {
		return 0;
	}

	icons_off = ST16_Read_BE32(data + 12);
	if (icons_off < ST16_ICONS_V1) {
		return 0;
	}
	blob_size = ST16_Read_BE32(data + 8);
	if (blob_size < ST16_ICONTROL_SIZE + ST16_CHUNK_TOTAL || blob_size > len) {
		return 0;
	}
	return 1;
}

int remix_st16_should_convert(const uint8_t *data, size_t len)
{
	if (!data || len < ST16_ICONTROL_SIZE) {
		return 0;
	}
	return ST16_Iconset_Should_Convert(data, len) ? 1 : 0;
}

int remix_st16_convert_payload(uint8_t *buf, size_t *inout_size)
{
	uint8_t scratch[ST16_CHUNKY_ICON_MAX_BYTES];
	size_t size;

	if (!buf || !inout_size || *inout_size < ST16_ICONTROL_SIZE) {
		return 0;
	}
	if (!C2P_Weights_Are_Ready()) {
		fprintf(stderr, "remix: ST16: C2P weights not installed\n");
		return 0;
	}
	if (remix_st16_is_native(buf, *inout_size)) {
		return 1;
	}
	if (!remix_st16_should_convert(buf, *inout_size)) {
		return 0;
	}

	size = *inout_size;
	if (!ST16_Convert_InPlace(buf, size, scratch)) {
		return 0;
	}
	*inout_size = (size_t)ST16_Read_BE32(buf + 8);
	return 1;
}

int remix_st16_prepare_theater_mix(int convert_st16_iconsets, const char *mix_basename, const char *w16_dir)
{
	char stem[32];

	if (!convert_st16_iconsets) {
		return 1;
	}
	if (!remix_st16_is_theater_mix(mix_basename)) {
		return 1;
	}
	if (!remix_st16_w16_stem_for_mix(mix_basename, stem, sizeof(stem))) {
		fprintf(stderr, "remix: ST16: unknown theater MIX %s\n", mix_basename ? mix_basename : "(null)");
		return 0;
	}
	return remix_st16_install_weights_from_file(w16_dir, stem);
}
