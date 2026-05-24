/*
 * subset_fix.h - Fixed pen -> palette index assignments.
 *
 * subset[pen] is the 256-color index for pen slot `pen` (see c2p.cpp).
 */

#ifndef PALETTE_OPT_SUBSET_FIX_H
#define PALETTE_OPT_SUBSET_FIX_H

#include <stdio.h>

#define PALETTE_OPT_SUBSET_FIX_NONE (-1)

typedef struct PaletteSubsetFix {
	int palette_index[16];
} PaletteSubsetFix;

void palette_subset_fix_clear(PaletteSubsetFix *fix);

/* pen 0..15, palette_index 0..255. Returns 0 on success. */
int palette_subset_fix_add(PaletteSubsetFix *fix, int pen, int palette_index);

/*
 * Parse "PEN,INDEX" or "PEN:INDEX". Returns 0 on success.
 * Also accepts repeated pairs in one string: "0,0,5,80".
 */
int palette_subset_fix_parse_pair(PaletteSubsetFix *fix, const char *spec);

int palette_subset_fix_count(const PaletteSubsetFix *fix);

/* Apply fixed pens into subset[0..n-1]. Returns 0 on success. */
int palette_subset_fix_apply(const PaletteSubsetFix *fix, unsigned char *subset, int n);

/*
 * Validate: pen in range, no duplicate palette_index among fixed entries, n <= 16.
 * Returns 0 on success.
 */
int palette_subset_fix_validate(const PaletteSubsetFix *fix, int n);

/* Returns 1 if every fixed pen matches subset[pen]. */
int palette_subset_fix_matches_subset(const PaletteSubsetFix *fix, const unsigned char *subset,
	int n);

int palette_subset_fix_is_fixed(const PaletteSubsetFix *fix, int pen);

void palette_subset_fix_log(const PaletteSubsetFix *fix, FILE *log);

#endif /* PALETTE_OPT_SUBSET_FIX_H */
