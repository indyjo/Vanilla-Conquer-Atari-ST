#!/usr/bin/env python3
"""Interactive viewer for an STVQ encode trace.

    vqatool encode --trace clip.stvi clip.vqa
    python3 stvqinspect.py clip.stvi

Requires Dear PyGui and NumPy (see requirements.txt).
"""

from __future__ import annotations

import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from trace import (  # noqa: E402
    STVI_DIST_NONE,
    Codebook,
    Trace,
    TraceError,
    feat_dist,
    feats_from_indices,
    indices_image,
    pens_image,
    split_planes,
    unpack_pens,
    vga6_palette,
)

try:
    import dearpygui.dearpygui as dpg
except ImportError:
    sys.stderr.write("stvqinspect needs dearpygui. Install with: pip install -r requirements.txt\n")
    raise

ZOOM_MAX = 6
TILE = 8
# Shared palette for the two heatmap tints.
HEAT_COLORS = {
    "Magenta": (255, 0, 255),
    "Cyan": (0, 255, 255),
    "Green": (0, 230, 70),
    "Yellow": (255, 196, 40),
    "Red": (255, 32, 32),
    "White": (255, 255, 255),
}
TOP_K = 8
THUMB_H = 72
LEFT_W = 780
MID_W = 150
PANE_H = 560
FILM_H = THUMB_H + 44
PANE_GAP = 4  # item spacing; keeps the film strip flush with the middle pane
MAIN_H = PANE_H - FILM_H - PANE_GAP
MIN_PANE_H = FILM_H + PANE_GAP + 64


def _nn(img: np.ndarray, z: int) -> np.ndarray:
    if z <= 1:
        return img
    return np.repeat(np.repeat(img, z, axis=0), z, axis=1)


def _rect(img: np.ndarray, x: int, y: int, w: int, h: int, color: tuple[int, int, int], t: int = 1) -> None:
    if w <= 0 or h <= 0:
        return
    x1 = min(img.shape[1], x + w)
    y1 = min(img.shape[0], y + h)
    x = max(0, x)
    y = max(0, y)
    img[y : min(img.shape[0], y + t), x:x1] = color
    img[max(y, y1 - t) : y1, x:x1] = color
    img[y:y1, x : min(img.shape[1], x + t)] = color
    img[y:y1, max(x, x1 - t) : x1] = color


# 5×7 glyphs, bit 4 is the leftmost pixel.
_GLYPH = {
    "0": (0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E),
    "1": (0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E),
    "2": (0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F),
    "3": (0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E),
    "4": (0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02),
    "5": (0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E),
    "6": (0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E),
    "7": (0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08),
    "8": (0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E),
    "9": (0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C),
    "-": (0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00),
    ".": (0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06),
}
_CELL_W = 36
_CELL_H = 22


def _fmt_coeff(value: float) -> str:
    sign = "-" if value < 0 else ""
    mag = abs(float(value))
    if mag >= 100:
        body = f"{mag:.0f}"
    elif mag >= 10:
        body = f"{mag:.1f}"
    else:
        body = f"{mag:.2f}"
    return (sign + body)[:5]


def _blit(img: np.ndarray, x: int, y: int, text: str, color: tuple[int, int, int]) -> None:
    h, w, _ = img.shape
    for ch in text:
        glyph = _GLYPH.get(ch)
        if glyph is None:
            x += 6
            continue
        for row, bits in enumerate(glyph):
            py = y + row
            if py < 0 or py >= h:
                continue
            for col in range(5):
                px = x + col
                if px < 0 or px >= w:
                    continue
                if bits & (1 << (4 - col)):
                    img[py, px] = color
        x += 6


def _plane_matrix(image: np.ndarray, codebook: np.ndarray) -> np.ndarray:
    """8×8 frequency matrix. Each cell is image over codebook. Color is their difference."""
    height = 8 * _CELL_H
    width = 8 * _CELL_W
    out = np.zeros((height, width, 3), dtype=np.uint8)
    out[:] = (16, 16, 18)
    diff = image - codebook
    finite = np.isfinite(diff)
    limit = float(np.max(np.abs(diff[finite]))) if np.any(finite) else 0.0
    for v in range(8):
        for u in range(8):
            y0 = v * _CELL_H
            x0 = u * _CELL_W
            cell = out[y0 + 1 : y0 + _CELL_H, x0 + 1 : x0 + _CELL_W]
            iv = float(image[v, u])
            cv = float(codebook[v, u])
            if not np.isfinite(iv) or not np.isfinite(cv):
                cell[:] = (28, 28, 32)
                continue
            delta = 0.0 if limit <= 1e-12 else max(-1.0, min(1.0, (iv - cv) / limit))
            if delta >= 0:
                bg = (36 + int(150 * delta), 32, 36)
            else:
                bg = (32, 32, 36 + int(150 * (-delta)))
            cell[:] = bg
            top = _fmt_coeff(iv)
            bot = _fmt_coeff(cv)
            _blit(out, x0 + 3, y0 + 2, top, (240, 240, 240))
            _blit(out, x0 + 3, y0 + 12, bot, (255, 196, 90))
    return out


def _thumb(img: np.ndarray, th: int = THUMB_H) -> np.ndarray:
    h, w, _ = img.shape
    if h <= th:
        return img
    tw = max(1, int(round(w * th / h)))
    ys = (np.arange(th) * h / th).astype(np.int32)
    xs = (np.arange(tw) * w / tw).astype(np.int32)
    return img[ys][:, xs]


class App:
    def __init__(self, trace: Trace):
        self.tr = trace
        self.book = Codebook(trace)
        self.frame_i = 0
        self.fr = None
        # -1 is the encoded result, 0 the source frame, 1+ a lookahead frame.
        self.view_win = 0
        self.zoom_left = 2
        self.zoom_right = 2
        self.sel_tile: int | None = None
        self.sel_cb: int | None = None
        self.show_decision = False
        self.show_sim = True
        self.show_elig = True
        self.sim_color = "Magenta"
        self.elig_color = "Yellow"
        self.sim_falloff = 1.0
        self._guard = False
        # Live float buffers, keyed by texture tag. Dear PyGui may keep the
        # pointer it was given, so these must stay alive and never be resized.
        self._tex_buf: dict[str, np.ndarray] = {}
        self._film_n = 0
        self._film_ids: list[int] = []
        self._atlas_cols = 1
        self._pane_h = PANE_H
        self._win_feats: dict[int, np.ndarray] = {}
        self._redraw_queued = False
        self._refit_queued = False
        self._tick_frame: int | None = None

    def load(self, index: int) -> None:
        self.frame_i = int(index)
        self.fr = self.tr.read_frame(self.frame_i)
        self._win_feats = {}
        self.book.at(self.frame_i)
        nwin = len(self.fr.wins)
        if nwin == 0:
            self.view_win = 0
        elif self.view_win >= nwin:
            self.view_win = nwin - 1
        if self.sel_tile is not None and self.sel_tile >= self.tr.tiles_n:
            self.sel_tile = None
        if self.sel_cb is not None and self.sel_cb >= self.tr.cb_entries:
            self.sel_cb = None
        self.redraw()

    def redraw(self) -> None:
        assert self.fr is not None
        fr = self.fr
        tr = self.tr
        current = self.view_win <= 0
        dpg.configure_item("decisions", enabled=current)
        base = self._main_image()
        win = None if self.view_win < 0 else fr.wins[self.view_win]
        win_i = None if self.view_win < 0 else self.view_win
        base = self._overlay_heat(base, win, win_i)
        if current and self.show_decision:
            self._decision_borders(base)
        if self.sel_tile is not None:
            col, row = divmod_tile(self.sel_tile, tr.tiles_y)
            _rect(base, col * 8, row * 8, 8, 8, (255, 255, 255), 1)
        shown = _nn(base, self.zoom_left)
        self._set_tex("main_tex", "main_image", shown)

        if self.view_win < 0:
            label = f"encoded frame {fr.index}"
        elif self.view_win == 0:
            label = f"source frame {fr.index}"
        else:
            label = self._lookahead_label()
        dpg.set_value("view_label", label)
        dpg.configure_item("view_label", wrap=self._content_width("main_scroll", LEFT_W - 24))

        self._draw_filmstrip()
        self._draw_atlas()
        self._draw_zooms()
        self._draw_text()
        self._draw_matrices()
        self._fit_panes()

    def _main_image(self) -> np.ndarray:
        fr = self.fr
        tr = self.tr
        assert fr is not None
        if self.view_win < 0:
            pens = unpack_pens(fr.recon)
            return pens_image(pens, tr.tiles_x, tr.tiles_y, fr.ste_rgb)
        win = fr.wins[self.view_win]
        assert win.src64 is not None
        return indices_image(win.src64, tr.tiles_x, tr.tiles_y, win.pal768)

    def _window_feats(self, win_index: int) -> np.ndarray:
        """DCT features of one lookahead frame's own blocks."""
        cached = self._win_feats.get(win_index)
        if cached is not None:
            return cached
        assert self.fr is not None
        win = self.fr.wins[win_index]
        assert win.src64 is not None
        tr = self.tr
        cached = feats_from_indices(
            win.src64,
            win.pal768,
            y_coeffs=tr.y_coeffs,
            chroma_coeffs=tr.chroma_coeffs,
            alpha=tr.alpha,
            chroma_weight=tr.chroma_weight,
            gamma=tr.gamma,
        )
        self._win_feats[win_index] = cached
        return cached

    def _similarity_heat(self, img: np.ndarray, feats: np.ndarray) -> np.ndarray:
        """Tint tiles that match the selected codebook entry. Others stay as they are."""
        assert self.fr is not None and self.sel_cb is not None
        dist = feat_dist(self.book.feats[self.sel_cb], feats, self.tr.ncoeff)
        d = dist.astype(np.float64)
        if d.size == 0:
            return img
        lo = float(d.min())
        hi = float(d.max())
        if hi <= lo:
            closeness = np.ones(d.shape, dtype=np.float32) if lo == 0.0 else np.zeros(d.shape, dtype=np.float32)
        else:
            closeness = (1.0 - (d - lo) / (hi - lo)).astype(np.float32)
        if self.sim_falloff != 1.0:
            closeness = np.power(closeness, self.sim_falloff)
        return self._blend_heat(img, closeness, self.sim_color)

    def _overlay_heat(self, img: np.ndarray, win, win_index: int | None) -> np.ndarray:
        """Eligibility first, then similarity. Each can be off on its own."""
        if self.show_elig and win is not None:
            img = self._candidate_heat(img, win.residual, win.pool)
        if self.show_sim and self.sel_cb is not None:
            assert self.fr is not None
            if win_index is not None and win_index > 0:
                feats = self._window_feats(win_index)
            else:
                feats = self.fr.feats
            img = self._similarity_heat(img, feats)
        return img

    def _blend_heat(self, img: np.ndarray, norm: np.ndarray, color_name: str) -> np.ndarray:
        """Blend each tile toward `color_name`. `norm` is per tile, 0..1."""
        grid = norm.reshape(self.tr.tiles_x, self.tr.tiles_y).T
        pix = np.repeat(np.repeat(grid, 8, axis=0), 8, axis=1)[..., None]
        color = np.array(HEAT_COLORS.get(color_name, HEAT_COLORS["Magenta"]), dtype=np.float32)
        base = img.astype(np.float32)
        strength = 0.72 * pix
        return np.clip(base * (1.0 - strength) + color * strength, 0, 255).astype(np.uint8)

    def _value_heat(self, img: np.ndarray, values: np.ndarray | None) -> np.ndarray:
        """Brighter color = larger per-tile value. Missing distances stay untinted."""
        if values is None or values.size == 0:
            return img
        missing = values == np.uint32(STVI_DIST_NONE)
        if bool(np.all(missing)):
            return img
        d = values.astype(np.float64)
        d[missing] = np.nan
        lo = float(np.nanmin(d))
        hi = float(np.nanmax(d))
        if hi <= lo:
            norm = np.zeros(values.shape, dtype=np.float32)
        else:
            norm = np.nan_to_num(((d - lo) / (hi - lo)).astype(np.float32), nan=0.0)
        return self._blend_heat(img, norm, self.elig_color)

    def _candidate_heat(self, img: np.ndarray, residual: np.ndarray | None, pool: int) -> np.ndarray:
        """Brighter color = larger pre-install residual = more likely to be shortlisted."""
        if residual is None or not pool:
            return img
        return self._value_heat(img, residual)

    def _lookahead_label(self) -> str:
        assert self.fr is not None
        win = self.fr.wins[self.view_win]
        k = self.view_win
        if win.residual is None:
            text = f"lookahead +{k} — no candidate residual in this trace (re-encode to record it)"
        elif not win.pool:
            text = f"lookahead +{k} — not a codebook candidate this frame"
        else:
            text = f"lookahead +{k}"
            if self.show_elig:
                text += " — brighter tiles are more likely to be installed"
        if self.sel_tile is not None and win.residual is not None:
            col, row = divmod_tile(self.sel_tile, self.tr.tiles_y)
            value = int(win.residual[self.sel_tile])
            shown = "n/a" if value == STVI_DIST_NONE else str(value)
            text += f"    tile ({col},{row}) residual {shown}"
        return text

    def _decision_borders(self, img: np.ndarray) -> None:
        assert self.fr is not None
        ty = self.tr.tiles_y
        for ti in range(self.tr.tiles_n):
            col, row = divmod_tile(ti, ty)
            color = (40, 210, 120) if self.fr.skip[ti] else (70, 140, 255)
            _rect(img, col * 8, row * 8, 8, 8, color, 1)

    def _content_width(self, tag: str, fallback: int) -> int:
        if not dpg.does_item_exist(tag):
            return fallback
        state = dpg.get_item_state(tag)
        avail = state.get("content_region_avail") or (0, 0)
        if avail[0] > 1:
            return int(avail[0])
        rect = state.get("rect_size") or (0, 0)
        if rect[0] > 1:
            return max(1, int(rect[0]) - 16)
        return fallback

    def _draw_filmstrip(self) -> None:
        assert self.fr is not None
        fr = self.fr
        slots: list[tuple[np.ndarray, int, str]] = []
        pens = unpack_pens(fr.recon)
        encoded = pens_image(pens, self.tr.tiles_x, self.tr.tiles_y, fr.ste_rgb)
        encoded = self._overlay_heat(encoded, None, None)
        lookahead_n = max(0, len(fr.wins) - 1)
        slots.append((encoded, -1, "Encoded"))
        for i, win in enumerate(fr.wins):
            assert win.src64 is not None
            img = indices_image(win.src64, self.tr.tiles_x, self.tr.tiles_y, win.pal768)
            img = self._overlay_heat(img, win, i)
            if i == 0:
                name = "Source"
            elif lookahead_n <= 1:
                name = "Lookahead"
            else:
                name = f"Lookahead +{i}"
            slots.append((img, i, name))
        self._sync_film_slots(len(slots))
        self._film_ids = [slot for _, slot, _ in slots]
        for i, (img, slot, name) in enumerate(slots):
            th = _thumb(img)
            if slot == self.view_win:
                _rect(th, 0, 0, th.shape[1], th.shape[0], (255, 220, 60), 2)
            self._set_tex(f"film_tex_{i}", f"film_img_{i}", th)
            dpg.set_value(f"film_lbl_{i}", name)

    def _sync_film_slots(self, n: int) -> None:
        if self._film_n == n and (n == 0 or dpg.does_item_exist(f"film_img_{n - 1}")):
            return
        for kid in dpg.get_item_children("film_row", 1) or []:
            dpg.delete_item(kid)
        self._film_n = n
        for i in range(n):
            with dpg.group(parent="film_row"):
                dpg.add_image("dummy_tex", tag=f"film_img_{i}")
                dpg.add_text("", tag=f"film_lbl_{i}")
            self._bind_film_click(f"film_img_{i}", i)
            self._bind_film_click(f"film_lbl_{i}", i)
            if i + 1 < n:
                dpg.add_spacer(width=8, parent="film_row")

    def _bind_film_click(self, item: str, index: int) -> None:
        # The click handler's sender is the handler itself, which has no alias.
        reg = f"film_click_{index}"
        if not dpg.does_item_exist(reg):
            with dpg.item_handler_registry(tag=reg):
                dpg.add_item_clicked_handler(
                    button=dpg.mvMouseButton_Left,
                    callback=self.on_film_click,
                    user_data=index,
                )
        dpg.bind_item_handler_registry(item, reg)

    def _draw_atlas(self) -> None:
        n = self.tr.cb_entries
        legend_w = self._content_width("atlas_scroll", 420)
        for tag in ("leg_dim", "leg_green", "leg_amber", "leg_white"):
            dpg.configure_item(tag, wrap=legend_w)
        if n == 0:
            img = np.zeros((8, 8, 3), dtype=np.uint8)
            self._atlas_cols = 1
            self._set_tex("atlas_tex", "atlas_image", img)
            return
        cell = TILE * self.zoom_right
        cols = max(1, min(n, max(cell, legend_w) // cell))
        rows = (n + cols - 1) // cols
        self._atlas_cols = cols
        atlas = np.zeros((rows * TILE, cols * TILE, 3), dtype=np.uint8)
        atlas[:] = (22, 22, 22)
        pens = unpack_pens(self.book.tiles)
        rgb = self.fr.ste_rgb[pens]
        dim = self.book.valid == 0
        rgb[dim] = (rgb[dim].astype(np.uint16) * 40 // 100).astype(np.uint8)
        top = set(self._top_cb())
        replaced = {rep.index for rep in self.fr.reps}
        for i in range(n):
            r, c = divmod(i, cols)
            y = r * TILE
            x = c * TILE
            atlas[y : y + TILE, x : x + TILE] = rgb[i]
            if i in replaced:
                _rect(atlas, x, y, TILE, TILE, (0, 220, 170), 1)
            if i in top:
                _rect(atlas, x, y, TILE, TILE, (255, 210, 40), 1)
            if i == self.sel_cb:
                _rect(atlas, x, y, TILE, TILE, (255, 255, 255), 1)
        self._set_tex("atlas_tex", "atlas_image", _nn(atlas, self.zoom_right))

    def _top_cb(self) -> list[int]:
        if self.sel_tile is None or self.tr.cb_entries == 0:
            return []
        valid = self.book.valid.astype(bool)
        if not np.any(valid):
            return []
        dist = feat_dist(self.fr.feats[self.sel_tile], self.book.feats[valid], self.tr.ncoeff)
        take = min(TOP_K, int(dist.shape[0]))
        order = np.argpartition(dist, take - 1)[:take]
        order = order[np.argsort(dist[order])]
        idxs = np.flatnonzero(valid)[order]
        return [int(i) for i in idxs]

    def _draw_zooms(self) -> None:
        blank = np.zeros((96, 96, 3), dtype=np.uint8)
        blank[:] = (28, 28, 28)
        tile_img = blank
        cb_img = blank.copy()
        shown = self._selected_tile_rgb()
        if shown is not None:
            tile_img = _nn(shown, 12)
        if self.sel_cb is not None:
            pen = unpack_pens(self.book.tiles[self.sel_cb : self.sel_cb + 1])[0]
            cb_img = _nn(self.fr.ste_rgb[pen], 12)
        self._set_tex("zoom_tile_tex", "zoom_tile", tile_img)
        self._set_tex("zoom_cb_tex", "zoom_cb", cb_img)

    def _selected_tile_rgb(self) -> np.ndarray | None:
        """8×8 RGB of the selected spot in the image currently on screen."""
        if self.sel_tile is None or self.fr is None:
            return None
        fr = self.fr
        if self.view_win < 0:
            assert fr.recon is not None
            pen = unpack_pens(fr.recon[self.sel_tile : self.sel_tile + 1])[0]
            return fr.ste_rgb[pen]
        win = fr.wins[self.view_win]
        assert win.src64 is not None
        block = win.src64[self.sel_tile].reshape(8, 8)
        return vga6_palette(win.pal768)[block]

    def _draw_text(self) -> None:
        fr = self.fr
        tr = self.tr
        assert fr is not None
        dpg.set_value(
            "frame_meta",
            f"frame {fr.index}/{tr.frame_count - 1}   seg {fr.segment}   "
            f"window {len(fr.wins)} (lookahead {len(fr.wins) - 1})   "
            f"STCR {fr.nrep}   tiles {tr.tiles_x}x{tr.tiles_y}   cb {tr.cb_entries}   "
            f"Y {tr.y_coeffs}   chroma {tr.chroma_coeffs}x2   "
            f"alpha {tr.alpha:g}   weight {tr.chroma_weight:g}   gamma {tr.gamma:g}",
        )
        if self.sel_tile is None:
            decision = "Click a tile."
        else:
            ti = self.sel_tile
            col, row = divmod_tile(ti, tr.tiles_y)
            n2 = int(fr.dist_n2[ti])
            n2s = "n/a" if n2 == STVI_DIST_NONE else str(n2)
            idx = int(fr.cb_index[ti])
            if fr.skip[ti]:
                how = f"skip, keep N−2   nearest cb {idx}"
            else:
                how = f"draw cb {idx}"
            decision = f"tile ({col},{row})  {how}   dist {int(fr.dist[ti])}   N−2 dist {n2s}"
        dpg.set_value("decision", decision)
        if self.sel_tile is not None and self.sel_cb is not None:
            dist = float(feat_dist(self.fr.feats[self.sel_tile], self.book.feats[self.sel_cb : self.sel_cb + 1], tr.ncoeff)[0])
            drawable = "drawable" if self.book.valid[self.sel_cb] else "not drawable this frame"
            pair = f"distance(image tile, cb {self.sel_cb}) = {int(dist)}   cb {self.sel_cb} is {drawable}"
        else:
            pair = "Select an image tile and a codebook tile to compare coefficients."
        dpg.set_value("pair", pair)
        if self.sel_tile is None:
            dpg.configure_item("use_encoded", enabled=False, label="Select encoded")
        else:
            idx = int(fr.cb_index[self.sel_tile])
            dpg.configure_item("use_encoded", enabled=0 <= idx < tr.cb_entries, label=f"Select cb {idx}")

    def _draw_matrices(self) -> None:
        blank = np.zeros((8 * _CELL_H, 8 * _CELL_W, 3), dtype=np.uint8)
        blank[:] = (28, 28, 32)
        if self.sel_tile is None or self.sel_cb is None:
            for tag in ("mat_y", "mat_u", "mat_v"):
                self._set_tex(tag + "_tex", tag, blank)
            self._set_coeff_note(
                "Select an image tile and a codebook tile.",
                "Each cell is image over codebook; empty cells were discarded into the tail.",
            )
            return
        assert self.fr is not None
        tr = self.tr
        yi, ui, vi, ti = split_planes(self.fr.feats[self.sel_tile], tr.y_coeffs, tr.chroma_coeffs)
        yb, ub, vb, tb = split_planes(self.book.feats[self.sel_cb], tr.y_coeffs, tr.chroma_coeffs)
        self._set_tex("mat_y_tex", "mat_y", _plane_matrix(yi, yb))
        self._set_tex("mat_u_tex", "mat_u", _plane_matrix(ui, ub))
        self._set_tex("mat_v_tex", "mat_v", _plane_matrix(vi, vb))

        def tail(name: str, a: float | None, b: float | None) -> str:
            if a is None or b is None:
                return ""
            return f"{name} {_fmt_coeff(a)}/{_fmt_coeff(b)}"

        tails = "   ".join(
            part
            for part in (
                tail("Ey", ti[0], tb[0]),
                tail("Eu", ti[1], tb[1]),
                tail("Ev", ti[2], tb[2]),
            )
            if part
        )
        self._set_coeff_note(
            "DC is the top-left cell, u increases to the right, v downward. "
            "White is the image tile, amber is the codebook tile. "
            "Cell color is their difference.",
            f"Tails (image/codebook)  {tails}" if tails else "No tail energies.",
        )

    def _set_coeff_note(self, legend: str, detail: str) -> None:
        dpg.configure_item("coeff_note", wrap=self._content_width("win", 1400))
        dpg.set_value("coeff_note", f"{legend}\n{detail}")

    def _fit_panes(self) -> None:
        """Shrink the panes so the two footer lines stay inside the window."""
        if not dpg.does_item_exist("coeff_note"):
            return
        win = dpg.get_item_state("win")
        note = dpg.get_item_state("coeff_note")
        win_size = win.get("rect_size") or (0, 0)
        pos = note.get("pos") or (0, 0)
        size = note.get("rect_size") or (0, 0)
        if win_size[1] < 100 or size[1] < 1:
            return
        slider_w = max(240, int(win_size[0]) - 16 - 28 - 28 - 24)
        if dpg.does_item_exist("frame_slider"):
            dpg.configure_item("frame_slider", width=slider_w)
        overflow = (pos[1] + size[1] + 10) - win_size[1]
        if overflow > 2:
            pane_h = max(MIN_PANE_H, self._pane_h - int(overflow))
        elif overflow < -16 and self._pane_h < PANE_H:
            pane_h = min(PANE_H, self._pane_h - int(overflow))
        else:
            return
        if pane_h == self._pane_h:
            return
        self._pane_h = pane_h
        for tag in ("left", "mid", "right", "atlas_scroll"):
            dpg.configure_item(tag, height=pane_h)
        dpg.configure_item("main_scroll", height=max(1, pane_h - FILM_H - PANE_GAP))
        self._refit_queued = True
        self._schedule_tick()

    def _schedule_tick(self) -> None:
        """One callback for the next frame. Slider drags share it so they cannot pile up."""
        frame = dpg.get_frame_count() + 1
        if self._tick_frame == frame:
            return
        self._tick_frame = frame
        dpg.set_frame_callback(frame, self._on_frame_tick)

    def _request_redraw(self) -> None:
        self._redraw_queued = True
        self._schedule_tick()

    def _on_frame_tick(self, *_) -> None:
        self._tick_frame = None
        if self._redraw_queued:
            self._redraw_queued = False
            self._apply_sliders()
            return
        if self._refit_queued and self.fr is not None:
            self._refit_queued = False
            self._fit_panes()

    def _apply_sliders(self) -> None:
        """Read the sliders as they are now, not the value from an earlier drag event."""
        zoom_right = int(dpg.get_value("zoom_right"))
        right_changed = zoom_right != self.zoom_right
        self.zoom_left = int(dpg.get_value("zoom_left"))
        self.zoom_right = zoom_right
        self.sim_falloff = float(dpg.get_value("sim_falloff"))
        frame = int(dpg.get_value("frame_slider"))
        if frame != self.frame_i:
            self.load(frame)
        elif self.fr is not None:
            self.redraw()
        if right_changed and self.sel_cb is not None:
            self._scroll_to_cb(self.sel_cb)

    def _set_tex(self, tex: str, image: str, rgb: np.ndarray) -> None:
        """Show `rgb` in `image`.

        Zoom changes the texture size. Deleting the previous raw texture from
        the slider callback frees it while the widget is still drawn, which
        segfaults. Keep one texture per size and rebind instead.
        """
        h, w, _ = rgb.shape
        if w <= 0 or h <= 0:
            return
        tag = f"{tex}_{w}x{h}"
        buf = self._tex_buf.get(tag)
        if buf is None or buf.size != w * h * 4:
            buf = np.empty(w * h * 4, dtype=np.float32)
            self._tex_buf[tag] = buf
        view = buf.reshape(h, w, 4)
        view[..., :3] = np.asarray(rgb, dtype=np.float32) * (1.0 / 255.0)
        view[..., 3] = 1.0
        if not dpg.does_item_exist(tag):
            dpg.add_raw_texture(w, h, buf, format=dpg.mvFormat_Float_rgba, tag=tag, parent="texreg")
        else:
            dpg.set_value(tag, buf)
        dpg.configure_item(image, texture_tag=tag, width=w, height=h)

    def on_frame(self, sender, app_data) -> None:
        if self._guard:
            return
        self._request_redraw()

    def on_toggle(self, sender, app_data) -> None:
        self.show_decision = bool(dpg.get_value("decisions"))
        self.show_sim = bool(dpg.get_value("heat_sim"))
        self.show_elig = bool(dpg.get_value("heat_elig"))
        self.redraw()

    def on_sim_color(self, sender, app_data) -> None:
        self.sim_color = str(app_data)
        self.redraw()

    def on_elig_color(self, sender, app_data) -> None:
        self.elig_color = str(app_data)
        self.redraw()

    def on_falloff(self, sender, app_data) -> None:
        self._request_redraw()

    def on_zoom_left(self, sender, app_data) -> None:
        self._request_redraw()

    def on_zoom_right(self, sender, app_data) -> None:
        self._request_redraw()

    def step(self, delta: int) -> None:
        nxt = min(self.tr.frame_count - 1, max(0, self.frame_i + delta))
        if nxt == self.frame_i:
            return
        self._guard = True
        dpg.set_value("frame_slider", nxt)
        self._guard = False
        self.load(nxt)

    def on_main_click(self, sender, app_data) -> None:
        x, y = self._rel("main_image")
        if x is None:
            return
        col = int(x) // (8 * self.zoom_left)
        row = int(y) // (8 * self.zoom_left)
        if col < 0 or row < 0 or col >= self.tr.tiles_x or row >= self.tr.tiles_y:
            return
        self.sel_tile = col * self.tr.tiles_y + row
        self.redraw()

    def on_use_encoded(self, sender, app_data) -> None:
        if self.sel_tile is None or self.fr is None:
            return
        encoded = int(self.fr.cb_index[self.sel_tile])
        if encoded < 0 or encoded >= self.tr.cb_entries:
            return
        self.sel_cb = encoded
        self.redraw()
        self._scroll_to_cb(self.sel_cb)

    def _scroll_to_cb(self, idx: int) -> None:
        cell = TILE * self.zoom_right
        row, col = divmod(idx, self._atlas_cols)
        width, height = dpg.get_item_rect_size("atlas_scroll")
        dpg.set_x_scroll("atlas_scroll", max(0, col * cell - max(0, width // 2 - cell // 2)))
        dpg.set_y_scroll("atlas_scroll", max(0, row * cell - max(0, height // 2 - cell // 2)))

    def on_atlas_click(self, sender, app_data) -> None:
        if self.tr.cb_entries == 0:
            return
        x, y = self._rel("atlas_image")
        if x is None:
            return
        cell = TILE * self.zoom_right
        col = int(x) // cell
        row = int(y) // cell
        idx = row * self._atlas_cols + col
        if idx < 0 or idx >= self.tr.cb_entries:
            return
        self.sel_cb = idx
        self.redraw()

    def on_film_click(self, sender, app_data, user_data=None) -> None:
        idx = user_data if isinstance(user_data, int) else None
        if idx is None:
            for item in (app_data, sender):
                alias = item if isinstance(item, str) else ""
                if not alias and item is not None and not isinstance(item, str):
                    try:
                        alias = dpg.get_item_alias(item)
                    except Exception:
                        alias = ""
                tail = str(alias).rsplit("_", 1)[-1]
                if tail.isdigit():
                    idx = int(tail)
                    break
        if idx is None or idx < 0 or idx >= len(self._film_ids):
            return
        self.view_win = self._film_ids[idx]
        self.redraw()

    def _rel(self, item: str) -> tuple[float, float] | tuple[None, None]:
        if not dpg.is_item_hovered(item):
            return None, None
        mx, my = dpg.get_mouse_pos(local=False)
        ix, iy = dpg.get_item_rect_min(item)
        return mx - ix, my - iy


def divmod_tile(index: int, tiles_y: int) -> tuple[int, int]:
    return index // tiles_y, index % tiles_y


def build(app: App) -> None:
    tr = app.tr
    dpg.create_context()
    dpg.create_viewport(title=f"STVQ inspect — {tr.path}", width=1480, height=960)
    with dpg.texture_registry(tag="texreg"):
        dummy = np.zeros(4, dtype=np.float32)
        dummy[3] = 1.0
        dpg.add_raw_texture(1, 1, dummy, format=dpg.mvFormat_Float_rgba, tag="dummy_tex")

    with dpg.window(tag="win"):
        with dpg.group(horizontal=True):
            dpg.add_button(label="<", width=28, callback=lambda *a: app.step(-1))
            dpg.add_slider_int(
                tag="frame_slider",
                label="",
                format="frame: %d",
                min_value=0,
                max_value=max(0, tr.frame_count - 1),
                default_value=0,
                width=1390,
                callback=app.on_frame,
            )
            dpg.add_button(label=">", width=28, callback=lambda *a: app.step(1))
        dpg.add_text("", tag="frame_meta")
        with dpg.group(horizontal=True):
            dpg.add_checkbox(label="Decisions", tag="decisions", default_value=False, callback=app.on_toggle)
            dpg.add_slider_int(
                tag="zoom_left",
                label="",
                format="image zoom: %d",
                min_value=1,
                max_value=ZOOM_MAX,
                default_value=app.zoom_left,
                width=180,
                callback=app.on_zoom_left,
            )
            dpg.add_slider_int(
                tag="zoom_right",
                label="",
                format="codebook zoom: %d",
                min_value=1,
                max_value=ZOOM_MAX,
                default_value=app.zoom_right,
                width=200,
                callback=app.on_zoom_right,
            )
            dpg.add_checkbox(label="Similarity", tag="heat_sim", default_value=True, callback=app.on_toggle)
            dpg.add_combo(
                tag="sim_color",
                items=list(HEAT_COLORS),
                default_value=app.sim_color,
                width=108,
                callback=app.on_sim_color,
            )
            dpg.add_slider_float(
                tag="sim_falloff",
                label="",
                format="falloff: %.2f",
                min_value=1.0,
                max_value=36.0,
                default_value=app.sim_falloff,
                width=180,
                callback=app.on_falloff,
            )
            dpg.add_checkbox(label="Eligibility", tag="heat_elig", default_value=True, callback=app.on_toggle)
            dpg.add_combo(
                tag="elig_color",
                items=list(HEAT_COLORS),
                default_value=app.elig_color,
                width=108,
                callback=app.on_elig_color,
            )
        with dpg.group(horizontal=True):
            with dpg.child_window(width=LEFT_W, height=PANE_H, tag="left", no_scrollbar=True, border=False):
                with dpg.child_window(width=-1, height=MAIN_H, tag="main_scroll", horizontal_scrollbar=True, border=True):
                    dpg.add_image("dummy_tex", tag="main_image")
                    dpg.add_text("", tag="view_label", wrap=LEFT_W - 24)
                with dpg.child_window(width=-1, height=FILM_H, tag="film_scroll", horizontal_scrollbar=True):
                    with dpg.group(horizontal=True, tag="film_row"):
                        pass
            with dpg.child_window(width=MID_W, height=PANE_H, tag="mid"):
                dpg.add_text("image tile")
                dpg.add_image("dummy_tex", tag="zoom_tile")
                dpg.add_text("codebook tile")
                dpg.add_image("dummy_tex", tag="zoom_cb")
                dpg.add_button(
                    label="Select encoded",
                    tag="use_encoded",
                    width=-1,
                    enabled=False,
                    callback=app.on_use_encoded,
                )
            with dpg.child_window(width=-1, height=PANE_H, tag="right", no_scrollbar=True, border=False):
                with dpg.child_window(width=-1, height=PANE_H, tag="atlas_scroll", border=True):
                    dpg.add_image("dummy_tex", tag="atlas_image")
                    dpg.add_text("Dim: not drawable this frame.", tag="leg_dim", wrap=420)
                    dpg.add_text("Green edge: installed this frame.", tag="leg_green", wrap=420)
                    dpg.add_text("Amber edge: closest to the selected image tile.", tag="leg_amber", wrap=420)
                    dpg.add_text("White edge: selected codebook tile.", tag="leg_white", wrap=420)
        dpg.add_text("", tag="decision")
        dpg.add_text("", tag="pair")
        with dpg.group(horizontal=True):
            for label, tag in (("Y", "mat_y"), ("U", "mat_u"), ("V", "mat_v")):
                with dpg.group():
                    dpg.add_text(label)
                    dpg.add_image("dummy_tex", tag=tag)
        dpg.add_text("", tag="coeff_note", wrap=1400)

    with dpg.item_handler_registry(tag="main_click"):
        dpg.add_item_clicked_handler(button=dpg.mvMouseButton_Left, callback=app.on_main_click)
    with dpg.item_handler_registry(tag="atlas_click"):
        dpg.add_item_clicked_handler(button=dpg.mvMouseButton_Left, callback=app.on_atlas_click)
    dpg.bind_item_handler_registry("main_image", "main_click")
    dpg.bind_item_handler_registry("atlas_image", "atlas_click")

    with dpg.handler_registry():
        dpg.add_key_press_handler(dpg.mvKey_Left, callback=lambda *a: app.step(-1))
        dpg.add_key_press_handler(dpg.mvKey_Right, callback=lambda *a: app.step(1))

    dpg.setup_dearpygui()
    dpg.show_viewport()
    dpg.set_primary_window("win", True)
    dpg.set_viewport_resize_callback(lambda *a: app.redraw() if app.fr is not None else None)
    app.load(0)
    dpg.set_frame_callback(2, lambda *a: app.redraw() if app.fr is not None else None)
    dpg.start_dearpygui()
    dpg.destroy_context()


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description="Inspect STVQ encoder decisions from an .stvi trace.")
    p.add_argument("trace", help="trace written by vqatool encode --trace")
    args = p.parse_args(argv)
    try:
        trace = Trace(args.trace)
    except TraceError as exc:
        sys.stderr.write(f"error: {exc}\n")
        return 1
    try:
        build(App(trace))
    finally:
        trace.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
