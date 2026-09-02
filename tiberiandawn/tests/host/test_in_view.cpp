/*
 * Host identity test: cell-index DisplayClass::In_View vs the former
 * Coord_Whole(Cell_Coord) lepton subtract.
 *
 * Packing matches function.h (little-endian COORDINATE numeric layout).
 * 64×64 map (MAP_CELL_MAX_X_BITS == 6).
 */

#include <cstdio>
#include <stdint.h>

enum {
	MAP_W = 64,
	MAP_H = 64,
	CELL_LEPTON = 256
};

typedef uint32_t Coordinate;
typedef short Cell;

static int Cell_X(Cell cell)
{
	return (int)((unsigned)cell & 63u);
}

static int Cell_Y(Cell cell)
{
	return (int)(((unsigned)cell >> 6) & 63u);
}

static Cell XY_Cell(int x, int y)
{
	return (Cell)((y << 6) | x);
}

/* X lepton bits 0–7, X cell 8–15, Y lepton 16–23, Y cell 24–31. */
static Coordinate Pack_Coord(int xcell, int xlep, int ycell, int ylep)
{
	return (Coordinate)((unsigned)xlep & 0xFFu)
		| ((Coordinate)((unsigned)xcell & 0xFFu) << 8)
		| ((Coordinate)((unsigned)ylep & 0xFFu) << 16)
		| ((Coordinate)((unsigned)ycell & 0xFFu) << 24);
}

static Coordinate Cell_Coord(Cell cell)
{
	return Pack_Coord(Cell_X(cell), CELL_LEPTON / 2, Cell_Y(cell), CELL_LEPTON / 2);
}

static Coordinate Coord_Whole(Coordinate coord)
{
	return coord & 0xFF00FF00u;
}

static int Coord_X(Coordinate coord)
{
	return (int)(uint16_t)(coord & 0xFFFFu);
}

static int Coord_Y(Coordinate coord)
{
	return (int)(uint16_t)(coord >> 16);
}

static int Coord_XCell(Coordinate coord)
{
	return (int)((coord >> 8) & 0xFFu);
}

static int Coord_YCell(Coordinate coord)
{
	return (int)((coord >> 24) & 0xFFu);
}

static bool In_View_Original(Cell cell, Coordinate tactical, int tac_w, int tac_h)
{
	Coordinate const coord = Coord_Whole(Cell_Coord(cell));
	Coordinate const tcoord = Coord_Whole(tactical);

	if ((Coord_X(coord) - Coord_X(tcoord)) > tac_w + 255)
		return false;
	if ((Coord_Y(coord) - Coord_Y(tcoord)) > tac_h + 255)
		return false;
	return true;
}

static bool In_View_New(Cell cell, Coordinate tactical, int tac_w, int tac_h)
{
	int const dx = Cell_X(cell) - Coord_XCell(tactical);
	if (dx < 0)
		return false;
	if (dx > ((tac_w + 255) >> 8))
		return false;

	int const dy = Cell_Y(cell) - Coord_YCell(tactical);
	if (dy < 0)
		return false;
	if (dy > ((tac_h + 255) >> 8))
		return false;

	return true;
}

static int Fail(Cell cell, Coordinate tac, int w, int h, char const* why)
{
	std::printf("FAIL %s cell=%d (%d,%d) tac_cell=(%d,%d) lep=(%d,%d) size=%d x %d old=%d new=%d\n",
		why,
		(int)cell,
		Cell_X(cell),
		Cell_Y(cell),
		Coord_XCell(tac),
		Coord_YCell(tac),
		(int)(tac & 0xFFu),
		(int)((tac >> 16) & 0xFFu),
		w,
		h,
		(int)In_View_Original(cell, tac, w, h),
		(int)In_View_New(cell, tac, w, h));
	return 1;
}

static int Compare_One(Coordinate tac, int w, int h, unsigned long* checked, unsigned long* left_top_diff)
{
	for (int y = 0; y < MAP_H; ++y) {
		for (int x = 0; x < MAP_W; ++x) {
			Cell const cell = XY_Cell(x, y);
			bool const old_v = In_View_Original(cell, tac, w, h);
			bool const new_v = In_View_New(cell, tac, w, h);
			int const dx = Cell_X(cell) - Coord_XCell(tac);
			int const dy = Cell_Y(cell) - Coord_YCell(tac);
			++*checked;

			if (dx >= 0 && dy >= 0) {
				if (old_v != new_v)
					return Fail(cell, tac, w, h, "mismatch with dx>=0 and dy>=0");
				continue;
			}

			/* New rejects left/above the camera; original signed lepton subtract does not. */
			if (new_v)
				return Fail(cell, tac, w, h, "new accepted dx<0 or dy<0");
			if (old_v != new_v)
				++*left_top_diff;
		}
	}
	return 0;
}

int test_in_view(void)
{
	static int const widths[] = {
		1, 255, 256, 257, 3328, 3329, 3583, 13 * 256, 13 * 256 + 1, 3300
	};
	static int const heights[] = {
		1, 255, 256, 2048, 8 * 256, 8 * 256 + 1, 1999
	};
	static int const leptons[] = {0, 1, 0x80, 0xFF};

	unsigned long checked = 0;
	unsigned long left_top_diff = 0;

	for (unsigned wi = 0; wi < sizeof(widths) / sizeof(widths[0]); ++wi) {
		for (unsigned hi = 0; hi < sizeof(heights) / sizeof(heights[0]); ++hi) {
			int const w = widths[wi];
			int const h = heights[hi];

			for (int ty = 0; ty < MAP_H; ty += 7) {
				for (int tx = 0; tx < MAP_W; tx += 5) {
					for (unsigned li = 0; li < sizeof(leptons) / sizeof(leptons[0]); ++li) {
						int const lep = leptons[li];
						Coordinate const tac = Pack_Coord(tx, lep, ty, lep);
						if (Compare_One(tac, w, h, &checked, &left_top_diff) != 0)
							return 1;
					}
				}
			}
		}
	}

	/* Dense cameras for one typical ST view size (cell-aligned and mid-lepton). */
	int const st_w = 13 * 256;
	int const st_h = 8 * 256;
	for (int ty = 0; ty < MAP_H; ++ty) {
		for (int tx = 0; tx < MAP_W; ++tx) {
			if (Compare_One(Pack_Coord(tx, 0, ty, 0), st_w, st_h, &checked, &left_top_diff) != 0)
				return 1;
			if (Compare_One(Pack_Coord(tx, 0x80, ty, 0x40), st_w, st_h, &checked, &left_top_diff) != 0)
				return 1;
		}
	}

	if (left_top_diff == 0) {
		std::printf("FAIL: expected left/top mismatches vs original signed lepton subtract\n");
		return 1;
	}

	std::printf(
		"PASS: In_View cell-index matches original when dx>=0 and dy>=0 (%lu checks, %lu left/top diffs)\n",
		checked,
		left_top_diff);
	return 0;
}
