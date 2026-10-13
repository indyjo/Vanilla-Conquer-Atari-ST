/*
 * palette_focus.c - Exclusive VGA-index ranges bound to hardware pen ranges.
 */

#include "palette_focus.h"

#include <stdlib.h>
#include <string.h>

static const PaletteFocus *g_focus;

void palette_focus_clear(PaletteFocus *focus)
{
	if (!focus)
		return;
	memset(focus, 0, sizeof(*focus));
}

void palette_focus_install(const PaletteFocus *focus)
{
	g_focus = focus && palette_focus_active(focus) ? focus : NULL;
}

const PaletteFocus *palette_focus_current(void)
{
	return g_focus;
}

int palette_focus_active(const PaletteFocus *focus)
{
	int i;

	if (!focus)
		return 0;
	if (focus->count > 0)
		return 1;
	for (i = 0; i < 256; i++) {
		if (focus->forbid[i])
			return 1;
	}
	return 0;
}

static int parse_span(const char *spec, const char **end_out, int *lo, int *hi, int max_value)
{
	char *end = NULL;
	long a;
	long b;

	if (!spec || !*spec)
		return -1;
	a = strtol(spec, &end, 10);
	if (end == spec)
		return -1;
	if (*end == '-') {
		b = strtol(end + 1, &end, 10);
		if (end == spec)
			return -1;
	} else {
		b = a;
	}
	if (a < 0 || b < 0 || a > max_value || b > max_value || a > b)
		return -1;
	*lo = (int)a;
	*hi = (int)b;
	if (end_out)
		*end_out = end;
	return 0;
}

int palette_focus_parse(PaletteFocus *focus, const char *spec)
{
	PaletteFocusGroup *g;
	const char *rest = NULL;
	int src_lo, src_hi, pen_lo, pen_hi;

	if (!focus || focus->count >= PALETTE_FOCUS_MAX_GROUPS)
		return -1;
	if (parse_span(spec, &rest, &src_lo, &src_hi, 255) != 0)
		return -1;
	if (!rest || *rest != ':')
		return -1;
	if (parse_span(rest + 1, &rest, &pen_lo, &pen_hi, 15) != 0)
		return -1;
	if (rest && *rest != '\0')
		return -1;

	g = &focus->group[focus->count++];
	g->src_lo = src_lo;
	g->src_hi = src_hi;
	g->pen_lo = pen_lo;
	g->pen_hi = pen_hi;
	return 0;
}

int palette_focus_parse_forbid(PaletteFocus *focus, const char *spec)
{
	const char *rest = NULL;
	int lo, hi, i;

	if (!focus)
		return -1;
	if (parse_span(spec, &rest, &lo, &hi, 255) != 0)
		return -1;
	if (rest && *rest != '\0')
		return -1;
	for (i = lo; i <= hi; i++)
		focus->forbid[i] = 1;
	return 0;
}

static int index_in_group(const PaletteFocusGroup *g, int index)
{
	return index >= g->src_lo && index <= g->src_hi;
}

static int pen_in_group(const PaletteFocusGroup *g, int pen)
{
	return pen >= g->pen_lo && pen <= g->pen_hi;
}

static int index_in_any_source(const PaletteFocus *focus, int index)
{
	int i;

	if (!focus)
		return 0;
	for (i = 0; i < focus->count; i++) {
		if (index_in_group(&focus->group[i], index))
			return 1;
	}
	return 0;
}

static int pen_in_any_group(const PaletteFocus *focus, int pen)
{
	int i;

	if (!focus)
		return 0;
	for (i = 0; i < focus->count; i++) {
		if (pen_in_group(&focus->group[i], pen))
			return 1;
	}
	return 0;
}

int palette_focus_pen_may_hold(const PaletteFocus *focus, int pen, int index)
{
	int i;
	int pen_reserved;
	int allowed;

	if (!focus || !palette_focus_active(focus))
		return 1;
	if (index < 0 || index > 255 || pen < 0 || pen > 15)
		return 0;
	if (focus->forbid[index])
		return 0;

	pen_reserved = pen_in_any_group(focus, pen);
	if (!pen_reserved)
		return !index_in_any_source(focus, index);

	allowed = 0;
	for (i = 0; i < focus->count; i++) {
		const PaletteFocusGroup *g = &focus->group[i];
		if (pen_in_group(g, pen) && index_in_group(g, index))
			allowed = 1;
	}
	return allowed;
}

int palette_focus_index_may_use_pen(const PaletteFocus *focus, int index, int pen)
{
	int i;
	int in_source;
	int pen_reserved;

	if (!focus || focus->count == 0)
		return 1;
	if (index < 0 || index > 255 || pen < 0 || pen > 15)
		return 0;

	in_source = index_in_any_source(focus, index);
	pen_reserved = pen_in_any_group(focus, pen);
	if (in_source) {
		for (i = 0; i < focus->count; i++) {
			const PaletteFocusGroup *g = &focus->group[i];
			if (index_in_group(g, index) && pen_in_group(g, pen))
				return 1;
		}
		return 0;
	}
	return !pen_reserved;
}

int palette_focus_validate(const PaletteFocus *focus, const PaletteSubsetFix *fix, int subset_n)
{
	int i;
	int pen;

	if (!focus || !palette_focus_active(focus))
		return 0;
	for (i = 0; i < focus->count; i++) {
		const PaletteFocusGroup *g = &focus->group[i];
		if (g->pen_hi >= subset_n) {
			fprintf(stderr, "error: --focus pen %d is outside subset size %d\n", g->pen_hi,
				subset_n);
			return -1;
		}
	}
	if (!fix)
		return 0;
	for (pen = 0; pen < subset_n; pen++) {
		int idx;
		if (!palette_subset_fix_is_fixed(fix, pen))
			continue;
		idx = fix->palette_index[pen];
		if (!palette_focus_pen_may_hold(focus, pen, idx)) {
			fprintf(stderr,
				"error: --fix pen %d index %d violates --focus/--forbid\n", pen, idx);
			return -1;
		}
	}
	return 0;
}

void palette_focus_log(const PaletteFocus *focus, FILE *log)
{
	int i;
	int nforbid = 0;

	if (!focus || !log || !palette_focus_active(focus))
		return;
	for (i = 0; i < focus->count; i++) {
		const PaletteFocusGroup *g = &focus->group[i];
		fprintf(log, "focus: sources %d-%d -> pens %d-%d\n", g->src_lo, g->src_hi, g->pen_lo,
			g->pen_hi);
	}
	for (i = 0; i < 256; i++) {
		if (focus->forbid[i])
			nforbid++;
	}
	if (nforbid)
		fprintf(log, "focus: %d source indices forbidden from the subset\n", nforbid);
}
