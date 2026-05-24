/*
 * subset_fix.c - Fixed pen -> palette index assignments for subset selection.
 */

#include "subset_fix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void palette_subset_fix_clear(PaletteSubsetFix *fix)
{
	int i;
	if (!fix)
		return;
	for (i = 0; i < 16; i++)
		fix->palette_index[i] = PALETTE_OPT_SUBSET_FIX_NONE;
}

int palette_subset_fix_add(PaletteSubsetFix *fix, int pen, int palette_index)
{
	if (!fix || pen < 0 || pen > 15 || palette_index < 0 || palette_index > 255)
		return -1;
	if (fix->palette_index[pen] >= 0) {
		fprintf(stderr, "error: duplicate fix for pen %d\n", pen);
		return -1;
	}
	{
		int i;
		for (i = 0; i < 16; i++) {
			if (i != pen && fix->palette_index[i] == palette_index) {
				fprintf(stderr,
					"error: palette index %d already fixed to pen %d (cannot also fix pen %d)\n",
					palette_index, i, pen);
				return -1;
			}
		}
	}
	fix->palette_index[pen] = palette_index;
	return 0;
}

int palette_subset_fix_parse_pair(PaletteSubsetFix *fix, const char *spec)
{
	const char *p = spec;
	int pair[32];
	int count = 0;

	if (!fix || !spec)
		return -1;

	while (*p) {
		char *end = NULL;
		long v = strtol(p, &end, 10);
		if (end == p)
			break;
		if (v < 0 || v > 255)
			return -1;
		if (count >= (int)(sizeof(pair) / sizeof(pair[0])))
			return -1;
		pair[count++] = (int)v;
		if (!*end)
			break;
		p = end;
		if (*p == ',' || *p == ':' || *p == ';')
			p++;
		while (*p == ' ' || *p == '\t')
			p++;
	}

	if (count == 0 || (count & 1) != 0)
		return -1;

	{
		int i;
		for (i = 0; i < count; i += 2) {
			if (palette_subset_fix_add(fix, pair[i], pair[i + 1]) != 0)
				return -1;
		}
	}
	return 0;
}

int palette_subset_fix_count(const PaletteSubsetFix *fix)
{
	int i;
	int n = 0;
	if (!fix)
		return 0;
	for (i = 0; i < 16; i++) {
		if (fix->palette_index[i] >= 0)
			n++;
	}
	return n;
}

int palette_subset_fix_apply(const PaletteSubsetFix *fix, unsigned char *subset, int n)
{
	int pen;
	if (!fix || !subset || n <= 0 || n > 16)
		return -1;
	for (pen = 0; pen < n; pen++) {
		if (fix->palette_index[pen] >= 0)
			subset[pen] = (unsigned char)fix->palette_index[pen];
	}
	return 0;
}

int palette_subset_fix_validate(const PaletteSubsetFix *fix, int n)
{
	int pen;
	if (!fix || n <= 0 || n > 16)
		return -1;
	for (pen = 0; pen < n; pen++) {
		if (fix->palette_index[pen] >= 0 && fix->palette_index[pen] > 255)
			return -1;
	}
	return 0;
}

int palette_subset_fix_matches_subset(const PaletteSubsetFix *fix, const unsigned char *subset,
	int n)
{
	int pen;
	if (!fix || !subset)
		return 0;
	for (pen = 0; pen < n; pen++) {
		if (palette_subset_fix_is_fixed(fix, pen) &&
			(int)subset[pen] != fix->palette_index[pen])
			return 0;
	}
	return 1;
}

int palette_subset_fix_is_fixed(const PaletteSubsetFix *fix, int pen)
{
	if (!fix || pen < 0 || pen > 15)
		return 0;
	return fix->palette_index[pen] >= 0;
}

void palette_subset_fix_log(const PaletteSubsetFix *fix, FILE *log)
{
	int pen;
	int any = 0;
	if (!fix || !log)
		return;
	for (pen = 0; pen < 16; pen++) {
		if (fix->palette_index[pen] >= 0) {
			if (!any) {
				fprintf(log, "subset fix: pen -> palette index\n");
				any = 1;
			}
			fprintf(log, "  pen %2d -> %3d\n", pen, fix->palette_index[pen]);
		}
	}
}
