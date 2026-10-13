/*
 * palette_focus.h - Exclusive source-index ranges mapped onto pen ranges.
 *
 * A focus group says: these VGA indices may mix only these hardware pens, and
 * those pens may not be used by any other index. Several groups may share a
 * pen range (their source indices are unioned). --forbid keeps an index out of
 * the subset without reserving a pen.
 */

#ifndef PALETTE_OPT_PALETTE_FOCUS_H
#define PALETTE_OPT_PALETTE_FOCUS_H

#include "subset_fix.h"

#include <stdio.h>

#define PALETTE_FOCUS_MAX_GROUPS 16

typedef struct PaletteFocusGroup {
	int src_lo;
	int src_hi;
	int pen_lo;
	int pen_hi;
} PaletteFocusGroup;

typedef struct PaletteFocus {
	int count;
	PaletteFocusGroup group[PALETTE_FOCUS_MAX_GROUPS];
	unsigned char forbid[256];
} PaletteFocus;

void palette_focus_clear(PaletteFocus *focus);

/* "SRC_LO-SRC_HI:PEN_LO-PEN_HI" or "SRC:PEN". Returns 0 on success. */
int palette_focus_parse(PaletteFocus *focus, const char *spec);

/* "LO-HI" or a single index. Returns 0 on success. */
int palette_focus_parse_forbid(PaletteFocus *focus, const char *spec);

int palette_focus_active(const PaletteFocus *focus);

/* Install the constraint used by weight search, spread, and annealing. */
void palette_focus_install(const PaletteFocus *focus);

const PaletteFocus *palette_focus_current(void);

/* 1 if pen may display this VGA index (subset assignment). */
int palette_focus_pen_may_hold(const PaletteFocus *focus, int pen, int index);

/* 1 if this source index may put weight on pen. */
int palette_focus_index_may_use_pen(const PaletteFocus *focus, int index, int pen);

/*
 * Fixed pens must sit inside their focus source range, and must not be forbidden.
 * Returns 0 on success.
 */
int palette_focus_validate(const PaletteFocus *focus, const PaletteSubsetFix *fix, int subset_n);

void palette_focus_log(const PaletteFocus *focus, FILE *log);

#endif /* PALETTE_OPT_PALETTE_FOCUS_H */
