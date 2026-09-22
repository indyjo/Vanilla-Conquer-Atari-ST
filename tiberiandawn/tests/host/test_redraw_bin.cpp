/*
 * Identity: lepton AABB vs cell-aligned redraw rects matches view-cell bitmasks.
 */

#include "../../redraw_bin.h"
#include <cstdio>

enum {
	CELL_LEP = 256,
	COLS = 16,
	ROWS = 12,
	ORIGIN_CX = 10,
	ORIGIN_CY = 20
};

struct FakeRect {
	int c0, r0, c1, r1;
};

static int lepton_hit(int ox0, int oy0, int ox1, int oy1, FakeRect const& rc)
{
	int const lx0 = (ORIGIN_CX + rc.c0) * CELL_LEP;
	int const ly0 = (ORIGIN_CY + rc.r0) * CELL_LEP;
	int const lx1 = (ORIGIN_CX + rc.c1) * CELL_LEP;
	int const ly1 = (ORIGIN_CY + rc.r1) * CELL_LEP;
	return Redraw_Lepton_Aabb_Hits_Rect(ox0, oy0, ox1, oy1, lx0, ly0, lx1, ly1);
}

static int mask_hit(int ox0, int oy0, int ox1, int oy1, FakeRect const& rc)
{
	unsigned long ocols;
	unsigned long orows;
	if (!Redraw_Clamp_Abs_Span(Redraw_Lepton_To_Cell(ox0), Redraw_Lepton_To_Cell(ox1),
			ORIGIN_CX, COLS, &ocols)) {
		return 0;
	}
	if (!Redraw_Clamp_Abs_Span(Redraw_Lepton_To_Cell(oy0), Redraw_Lepton_To_Cell(oy1),
			ORIGIN_CY, ROWS, &orows)) {
		return 0;
	}
	unsigned long const rcol = Redraw_Cell_Bitspan(rc.c0, rc.c1);
	unsigned long const rrow = Redraw_Cell_Bitspan(rc.r0, rc.r1);
	return (ocols & rcol) != 0 && (orows & rrow) != 0;
}

int test_redraw_bin(void)
{
	FakeRect const rects[] = {
		{0, 0, 2, 2},
		{4, 1, 7, 3},
		{0, 5, 16, 6},
		{15, 11, 16, 12},
	};
	int const nrect = (int)(sizeof(rects) / sizeof(rects[0]));
	int fails = 0;
	int cases = 0;

	for (int dx = -2; dx <= COLS + 1; dx++) {
		for (int dy = -2; dy <= ROWS + 1; dy++) {
			for (int w = 1; w <= 3; w++) {
				int const ox0 = (ORIGIN_CX + dx) * CELL_LEP - 40;
				int const oy0 = (ORIGIN_CY + dy) * CELL_LEP - 40;
				int const ox1 = ox0 + w * CELL_LEP;
				int const oy1 = oy0 + w * CELL_LEP;
				for (int ri = 0; ri < nrect; ri++) {
					int const a = lepton_hit(ox0, oy0, ox1, oy1, rects[ri]);
					int const b = mask_hit(ox0, oy0, ox1, oy1, rects[ri]);
					cases++;
					if (a != b) {
						std::printf("mismatch dx=%d dy=%d w=%d ri=%d lepton=%d mask=%d\n",
							dx, dy, w, ri, a, b);
						fails++;
						if (fails > 8) {
							return 1;
						}
					}
				}
			}
		}
	}

	/* Point on a shared vertical edge: ox1 == next rect lx0 still counts as a hit. */
	FakeRect const edge = {2, 0, 4, 2};
	int const lx = (ORIGIN_CX + 2) * CELL_LEP;
	int const ly0 = (ORIGIN_CY + 0) * CELL_LEP;
	int const ly1 = ly0 + 10;
	if (!lepton_hit(lx - 8, ly0, lx, ly1, edge) || !mask_hit(lx - 8, ly0, lx, ly1, edge)) {
		std::printf("edge-inclusive hit failed\n");
		return 1;
	}

	if (fails != 0) {
		return 1;
	}
	std::printf("redraw_bin cases=%d ok\n", cases);
	return 0;
}
