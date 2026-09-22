#ifndef REDRAW_BIN_H
#define REDRAW_BIN_H

/*
 * Object-to-rect binning helpers for ST_Redraw_Coalesced_Clipped.
 * CELL_LEPTON_W/H must be 256 (lepton→cell is arithmetic >> 8).
 */

static inline int Redraw_Lepton_To_Cell(int lepton)
{
	return lepton >> 8;
}

static inline unsigned long Redraw_Cell_Bitspan(int lo, int hi_exclusive)
{
	return ((1UL << hi_exclusive) - 1UL) ^ ((1UL << lo) - 1UL);
}

static inline int Redraw_Clamp_Abs_Span(int a0, int a1_incl, int origin, int vis, unsigned long* bits)
{
	int v0 = a0 - origin;
	int v1 = a1_incl - origin;
	if (v0 < 0) {
		v0 = 0;
	}
	if (v1 >= vis) {
		v1 = vis - 1;
	}
	if (v0 > v1) {
		*bits = 0;
		return 0;
	}
	*bits = Redraw_Cell_Bitspan(v0, v1 + 1);
	return 1;
}

static inline int Redraw_Lepton_Aabb_Hits_Rect(int ox0, int oy0, int ox1, int oy1,
	int lx0, int ly0, int lx1, int ly1)
{
	return ox0 < lx1 && ox1 >= lx0 && oy0 < ly1 && oy1 >= ly0;
}

#endif
