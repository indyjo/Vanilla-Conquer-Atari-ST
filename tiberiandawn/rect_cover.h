#ifndef TIBERIANDAWN_RECT_COVER_H
#define TIBERIANDAWN_RECT_COVER_H

/*
**	Greedy rectangle cover of marked cells on a row-major grid.
**
**	Idx is the coordinate / count type (short on Atari ST, int elsewhere).
**
**	remain[r * cols + c] is treated as marked when non-zero. Covering a
**	rectangle unmarks every cell inside it (marked and unmarked). Unmarked
**	cells may be included when an expansion still meets the density test.
**
**	Each greedy rectangle starts at the first remaining marked cell in
**	row-major order and expands right, down, or left (never up) while the
**	new edge strip still contains a mark and
**	    marked * density_den >= density_num * area
**	Among legal moves, the one that covers the most remaining marks wins.
**	Grow keeps running marked counts for the current rect and for the
**	strips immediately left, right, and below; only the new edge is
**	rescanned after a move.
**	At most greedy_max such rectangles; any leftover marks become one
**	axis-aligned bounding box if out_cap allows.
**
**	Rectangles are half-open: [c0, c1) × [r0, r1).
*/

template <class Idx>
struct Rect_Cover {
	Idx c0;
	Idx r0;
	Idx c1;
	Idx r1;
};

template <class Cell, class Idx>
inline Idx Rect_Cover_Count(Cell const* grid, Idx cols, Idx r0, Idx r1, Idx c0, Idx c1)
{
	Idx n = 0;
	for (Idx r = r0; r < r1; r++) {
		Cell const* row = grid + r * cols + c0;
		for (Idx c = c0; c < c1; c++) {
			if (*row++) {
				n++;
			}
		}
	}
	return n;
}

template <class Cell, class Idx>
inline Idx Rect_Cover_Count_Col(Cell const* grid, Idx cols, Idx c, Idx r0, Idx r1)
{
	Idx n = 0;
	Cell const* p = grid + r0 * cols + c;
	for (Idx r = r0; r < r1; r++) {
		if (*p) {
			n++;
		}
		p += cols;
	}
	return n;
}

template <class Cell, class Idx>
inline Idx Rect_Cover_Count_Row(Cell const* grid, Idx cols, Idx r, Idx c0, Idx c1)
{
	Idx n = 0;
	Cell const* p = grid + r * cols + c0;
	for (Idx c = c0; c < c1; c++) {
		if (*p++) {
			n++;
		}
	}
	return n;
}

template <class Cell, class Idx>
inline Idx Rect_Cover_Greedy(Cell* remain,
                             Idx rows,
                             Idx cols,
                             Rect_Cover<Idx>* out,
                             Idx out_cap,
                             Idx greedy_max,
                             Idx density_num,
                             Idx density_den)
{
	if (remain == 0 || out == 0 || rows <= 0 || cols <= 0 || out_cap <= 0) {
		return 0;
	}
	if (greedy_max > out_cap) {
		greedy_max = out_cap;
	}
	if (greedy_max < 0) {
		greedy_max = 0;
	}
	if (density_den <= 0) {
		return 0;
	}

	Idx leftover = Rect_Cover_Count<Cell, Idx>(remain, cols, 0, rows, 0, cols);
	Idx nrect = 0;

	while (nrect < greedy_max && leftover > 0) {
		Idx sr = -1;
		Idx sc = -1;
		for (Idx r = 0; r < rows && sr < 0; r++) {
			for (Idx c = 0; c < cols; c++) {
				if (remain[r * cols + c]) {
					sr = r;
					sc = c;
					break;
				}
			}
		}
		if (sr < 0) {
			break;
		}

		Idx r0 = sr;
		Idx c0 = sc;
		Idx r1 = (Idx)(sr + 1);
		Idx c1 = (Idx)(sc + 1);
		Idx marked = 1;
		Idx right = (c1 < cols) ? Rect_Cover_Count_Col<Cell, Idx>(remain, cols, c1, r0, r1) : 0;
		Idx left = (c0 > 0) ? Rect_Cover_Count_Col<Cell, Idx>(remain, cols, (Idx)(c0 - 1), r0, r1) : 0;
		Idx below = (r1 < rows) ? Rect_Cover_Count_Row<Cell, Idx>(remain, cols, r1, c0, c1) : 0;
		for (;;) {
			Idx best_marked = -1;
			Idx best_move = 0;
			if (c1 < cols && right > 0) {
				Idx const m = (Idx)(marked + right);
				Idx const area = (Idx)((r1 - r0) * (c1 + 1 - c0));
				if (m * density_den >= density_num * area && m > best_marked) {
					best_marked = m;
					best_move = 1;
				}
			}
			if (r1 < rows && below > 0) {
				Idx const m = (Idx)(marked + below);
				Idx const area = (Idx)((r1 + 1 - r0) * (c1 - c0));
				if (m * density_den >= density_num * area && m > best_marked) {
					best_marked = m;
					best_move = 2;
				}
			}
			if (c0 > 0 && left > 0) {
				Idx const m = (Idx)(marked + left);
				Idx const area = (Idx)((r1 - r0) * (c1 - (c0 - 1)));
				if (m * density_den >= density_num * area && m > best_marked) {
					best_marked = m;
					best_move = 3;
				}
			}
			if (best_move == 1) {
				marked = (Idx)(marked + right);
				if (r1 < rows && remain[r1 * cols + c1]) {
					below++;
				}
				c1++;
				right = (c1 < cols) ? Rect_Cover_Count_Col<Cell, Idx>(remain, cols, c1, r0, r1) : 0;
			} else if (best_move == 2) {
				marked = (Idx)(marked + below);
				if (c1 < cols && remain[r1 * cols + c1]) {
					right++;
				}
				if (c0 > 0 && remain[r1 * cols + (c0 - 1)]) {
					left++;
				}
				r1++;
				below = (r1 < rows) ? Rect_Cover_Count_Row<Cell, Idx>(remain, cols, r1, c0, c1) : 0;
			} else if (best_move == 3) {
				marked = (Idx)(marked + left);
				if (r1 < rows && remain[r1 * cols + (c0 - 1)]) {
					below++;
				}
				c0--;
				left = (c0 > 0) ? Rect_Cover_Count_Col<Cell, Idx>(remain, cols, (Idx)(c0 - 1), r0, r1) : 0;
			} else {
				break;
			}
		}

		for (Idx r = r0; r < r1; r++) {
			for (Idx c = c0; c < c1; c++) {
				remain[r * cols + c] = 0;
			}
		}
		leftover = (Idx)(leftover - marked);
		out[nrect].c0 = c0;
		out[nrect].r0 = r0;
		out[nrect].c1 = c1;
		out[nrect].r1 = r1;
		nrect++;
	}

	if (leftover > 0 && nrect < out_cap) {
		Idx r0 = rows;
		Idx r1 = 0;
		Idx c0 = cols;
		Idx c1 = 0;
		for (Idx r = 0; r < rows; r++) {
			for (Idx c = 0; c < cols; c++) {
				if (remain[r * cols + c]) {
					if (r < r0) {
						r0 = r;
					}
					if ((Idx)(r + 1) > r1) {
						r1 = (Idx)(r + 1);
					}
					if (c < c0) {
						c0 = c;
					}
					if ((Idx)(c + 1) > c1) {
						c1 = (Idx)(c + 1);
					}
				}
			}
		}
		if (r1 > r0 && c1 > c0) {
			for (Idx r = r0; r < r1; r++) {
				for (Idx c = c0; c < c1; c++) {
					remain[r * cols + c] = 0;
				}
			}
			out[nrect].c0 = c0;
			out[nrect].r0 = r0;
			out[nrect].c1 = c1;
			out[nrect].r1 = r1;
			nrect++;
		}
	}

	return nrect;
}

#endif
