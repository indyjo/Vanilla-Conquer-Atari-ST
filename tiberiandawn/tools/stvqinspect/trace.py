"""Read an STVI encode-decision trace written by ``vqatool encode --trace``.

The layout is documented in ``tiberiandawn/tools/vqatool/stvq_trace.h``.
Multi-byte fields are little-endian. ``index_off == 0`` means the encode
did not finish and the footer was never written.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field

import numpy as np

STVI_VERSION = 1
STVI_F_FULL = 1
STVI_F_WIN_RESIDUAL = 2
STVI_F_UTIL = 4
STVI_DIST_NONE = 0xFFFFFFFF
STVI_CB_NONE = 0xFFFF
STVI_SCALE = 65536.0

_HDR = struct.Struct("<4s10H I 3f Q 16s")
assert _HDR.size == 64


class TraceError(Exception):
    pass


def zigzag_uv() -> list[tuple[int, int]]:
    """Frequency (u, v) for each zig-zag slot. Same walk as ``stvq_metric.c``."""
    u = 0
    v = 0
    coords: list[tuple[int, int]] = []
    for _ in range(64):
        coords.append((u, v))
        if (u + v) & 1:
            if u == 7:
                v += 1
            elif v == 0:
                u += 1
            else:
                u += 1
                v -= 1
        else:
            if v == 7:
                u += 1
            elif u == 0:
                v += 1
            else:
                u -= 1
                v += 1
    return coords


ZZ_UV = zigzag_uv()


def split_planes(feat: np.ndarray, y_coeffs: int, chroma_coeffs: int) -> tuple[np.ndarray, np.ndarray, np.ndarray, tuple[float | None, float | None, float | None]]:
    """Scatter kept weighted coeffs onto 8×8 planes. Missing positions are NaN.

    Returns Y, U, V grids (row = v, column = u) and tail energies (Ey, Eu, Ev).
    """
    feat = np.asarray(feat, dtype=np.float64)
    i = 0

    def take(n: int) -> np.ndarray:
        nonlocal i
        chunk = feat[i : i + n]
        i += n
        if chunk.shape[0] < n:
            chunk = np.pad(chunk, (0, n - chunk.shape[0]))
        return chunk

    y = take(y_coeffs)
    u = take(chroma_coeffs)
    v = take(chroma_coeffs)
    ey = eu = ev = None
    if y_coeffs < 64 and i < feat.shape[0]:
        ey = float(feat[i])
        i += 1
    if chroma_coeffs < 64 and i + 1 < feat.shape[0]:
        eu = float(feat[i])
        ev = float(feat[i + 1])

    def place(values: np.ndarray) -> np.ndarray:
        grid = np.full((8, 8), np.nan, dtype=np.float64)
        for k, value in enumerate(values):
            cu, cv = ZZ_UV[k]
            grid[cv, cu] = value
        return grid

    return place(y), place(u), place(v), (ey, eu, ev)


def coeff_names(y_coeffs: int, chroma_coeffs: int, feat_len: int) -> list[str]:
    """Names for the metric feature lanes, including tail energies."""
    names = [f"Y{i}" for i in range(y_coeffs)]
    names += [f"U{i}" for i in range(chroma_coeffs)]
    names += [f"V{i}" for i in range(chroma_coeffs)]
    if y_coeffs < 64:
        names.append("Ey")
    if chroma_coeffs < 64:
        names.append("Eu")
        names.append("Ev")
    while len(names) < feat_len:
        names.append(f"f{len(names)}")
    return names[:feat_len]


def feat_dist(one: np.ndarray, many: np.ndarray, ncoeff: int) -> np.ndarray:
    """Scaled SSE of kept coeffs, plus the sum of tail energies.

    ``one`` is (F,), ``many`` is (M, F). Matches ``stvq_metric_feat_dist``
    for a full (unlimited) compare: float32 accumulation, then
    ``floor(sum * 65536 + 0.5)``. An identical feature vector is distance 0,
    matching the encoder's color-hash short circuit for a tile that really
    matches. The tail sum would otherwise charge ``2E`` for that pair.
    """
    one = np.asarray(one, dtype=np.float32)
    many = np.asarray(many, dtype=np.float32)
    delta = many[:, :ncoeff] - one[:ncoeff]
    sse = np.sum(delta * delta, axis=1, dtype=np.float32)
    if one.shape[0] > ncoeff:
        sse = sse + np.sum(many[:, ncoeff:] + one[ncoeff:], axis=1, dtype=np.float32)
    dist = np.floor(sse.astype(np.float64) * STVI_SCALE + 0.5)
    dist[np.all(many == one, axis=1)] = 0.0
    return dist


def feats_from_indices(
    src64: np.ndarray,
    pal768: np.ndarray,
    *,
    y_coeffs: int,
    chroma_coeffs: int,
    alpha: float,
    chroma_weight: float,
    gamma: float,
) -> np.ndarray:
    """Weighted YUV-DCT features of VGA-index tiles.

    ``src64`` is (tiles, 64), row-major 8×8. Same transform as
    ``stvq_metric_feat_from_indices`` with ``y_scale`` 1: full 6-bit channels,
    gamma, then YUV. Used for lookahead blocks, which the trace stores as
    indices rather than encoder features.
    """
    idx = np.asarray(src64, dtype=np.uint8).reshape(-1, 64)
    n = idx.shape[0]
    ny = max(1, min(64, int(y_coeffs)))
    nc = max(0, min(64, int(chroma_coeffs)))
    feat_len = (ny + (ny < 64)) + 2 * (nc + (nc < 64))
    out = np.zeros((n, feat_len), dtype=np.float32)
    if n == 0:
        return out

    c6 = np.bitwise_and(np.asarray(pal768, dtype=np.uint8).reshape(256, 3), 63)
    lin = np.power(c6.astype(np.float32) * np.float32(1.0 / 63.0), np.float32(gamma))
    r, g, b = lin[:, 0], lin[:, 1], lin[:, 2]
    y = np.float32(0.299) * r + np.float32(0.587) * g + np.float32(0.114) * b
    yuv = np.stack((y, np.float32(0.492) * (b - y), np.float32(0.877) * (r - y)), axis=1)

    k = np.arange(8, dtype=np.float32)
    npos = (np.arange(8, dtype=np.float32) + np.float32(0.5))
    cos = np.cos(np.float32(np.pi / 8.0) * npos[None, :] * k[:, None]).astype(np.float32)
    scale = np.full(8, np.float32(np.sqrt(2.0 / 8.0)), dtype=np.float32)
    scale[0] = np.float32(1.0 / np.sqrt(8.0))
    uu = np.array([u for u, _v in ZZ_UV], dtype=np.int32)
    vv = np.array([v for _u, v in ZZ_UV], dtype=np.int32)
    sqrt_w = np.sqrt(1.0 / (1.0 + np.float32(alpha) * (uu * uu + vv * vv))).astype(np.float32)

    def plane(comp: int) -> np.ndarray:
        block = yuv[idx, comp].reshape(n, 8, 8)
        tmp = np.einsum("nyx,ux->nyu", block, cos, optimize=True) * scale
        coeff = np.einsum("nyu,vy->nvu", tmp, cos, optimize=True) * scale.reshape(1, 8, 1)
        return coeff

    def pack(coeff: np.ndarray, count: int, kept_scale: float, tail_scale: float, dest: np.ndarray, at: int) -> int:
        if count:
            kept = kept_scale * sqrt_w[:count] * coeff[:, vv[:count], uu[:count]]
            dest[:, at : at + count] = kept
            at += count
        if count < 64 and tail_scale != 0.0:
            tail = sqrt_w[count:] * coeff[:, vv[count:], uu[count:]]
            dest[:, at] = tail_scale * np.sum(tail * tail, axis=1, dtype=np.float32)
            at += 1
        elif count < 64:
            at += 1
        return at

    ycoeff = plane(0)
    cursor = pack(ycoeff, ny, 1.0, 1.0, out, 0)
    if chroma_weight == 0.0:
        return out
    cw = float(chroma_weight)
    cscale = float(np.sqrt(cw))
    cursor = pack(plane(1), nc, cscale, cw, out, cursor)
    pack(plane(2), nc, cscale, cw, out, cursor)
    return out


def vga6_to_ste_rgb(rgb6: np.ndarray) -> np.ndarray:
    """STE RGB8 of VGA6 colors. Same pack/unpack as ``stvq_vga6_to_ste``."""
    c4 = ((np.asarray(rgb6, dtype=np.int32) & 63) * 15 + 31) // 63
    nib = ((c4 >> 1) & 7) | ((c4 & 1) << 3)
    c4b = ((nib & 7) << 1) | ((nib >> 3) & 1)
    return ((c4b << 4) | c4b).astype(np.uint8)


def subset_from_ste(ste_rgb: np.ndarray, pal768: np.ndarray) -> np.ndarray:
    """Pen → VGA index whose STE color matches ``ste_rgb``.

    Several VGA entries can share one STE color. The first match is kept.
    """
    pal = np.bitwise_and(np.asarray(pal768, dtype=np.uint8).reshape(256, 3), 63)
    ste = vga6_to_ste_rgb(pal)
    want = np.asarray(ste_rgb, dtype=np.uint8).reshape(16, 3)
    subset = np.zeros(16, dtype=np.uint8)
    for pen in range(16):
        hits = np.flatnonzero(np.all(ste == want[pen], axis=1))
        if hits.size:
            subset[pen] = np.uint8(hits[0])
    return subset


def vga6_palette(pal768: np.ndarray) -> np.ndarray:
    """VGA6 palette (256×3, channels 0..63) to RGB8."""
    p = np.asarray(pal768, dtype=np.uint16).reshape(256, 3)
    return ((p << 2) | (p >> 4)).astype(np.uint8)


def unpack_pens(tiles32: np.ndarray) -> np.ndarray:
    """(N, 32) planar tiles to (N, 8, 8) pen indices, row-major within a tile."""
    rows = np.asarray(tiles32, dtype=np.uint8).reshape(-1, 8, 4)
    bit = (1 << np.arange(7, -1, -1)).astype(np.uint8)
    on = (rows[..., None] & bit) != 0  # y, plane, x
    pen = (
        on[..., 0, :].astype(np.uint8)
        | (on[..., 1, :].astype(np.uint8) << 1)
        | (on[..., 2, :].astype(np.uint8) << 2)
        | (on[..., 3, :].astype(np.uint8) << 3)
    )
    return pen


def pens_image(pens_n_yx: np.ndarray, tiles_x: int, tiles_y: int, pal_rgb: np.ndarray) -> np.ndarray:
    """(tiles_n, 8, 8) pens, column-major tile order, to an RGB image."""
    grid = pens_n_yx.reshape(tiles_x, tiles_y, 8, 8)
    grid = grid.transpose(1, 2, 0, 3).reshape(tiles_y * 8, tiles_x * 8)
    return np.asarray(pal_rgb, dtype=np.uint8)[grid]


def indices_image(src64: np.ndarray, tiles_x: int, tiles_y: int, pal768: np.ndarray) -> np.ndarray:
    """Column-major VGA indices (tiles_n, 64) to an RGB image."""
    idx = np.asarray(src64, dtype=np.uint8).reshape(tiles_x, tiles_y, 8, 8)
    idx = idx.transpose(1, 2, 0, 3).reshape(tiles_y * 8, tiles_x * 8)
    return vga6_palette(pal768)[idx]


@dataclass
class WinImage:
    seg: int
    pal768: np.ndarray
    src64: np.ndarray | None
    pool: int = 0
    residual: np.ndarray | None = None


@dataclass
class UtilEvent:
    """One stage-2 decision. cb_index is None when the tile stopped the run."""

    pass_index: int
    cb_index: int | None
    wi: int
    predicted: int
    actual: int


@dataclass
class Replace:
    index: int
    drawable: int
    tile: np.ndarray
    feat: np.ndarray


@dataclass
class Frame:
    index: int
    segment: int
    flags: int
    ste_rgb: np.ndarray
    wins: list[WinImage]
    recon: np.ndarray | None
    full: tuple[np.ndarray, np.ndarray, np.ndarray] | None
    reps: list[Replace]
    cb_index: np.ndarray
    skip: np.ndarray
    dist: np.ndarray
    dist_n2: np.ndarray
    feats: np.ndarray
    nscore: int | None = None
    util_events: list[UtilEvent] = field(default_factory=list)
    nrep: int = field(init=False)

    def __post_init__(self) -> None:
        self.nrep = len(self.reps)


class Trace:
    def __init__(self, path: str):
        self.path = path
        self.fp = open(path, "rb")
        hdr = self.fp.read(_HDR.size)
        if len(hdr) != _HDR.size:
            raise TraceError("short STVI header")
        fields = _HDR.unpack(hdr)
        magic = fields[0]
        if magic != b"STVI":
            raise TraceError(f"not an STVI trace (magic {magic!r})")
        version = fields[1]
        if version != STVI_VERSION:
            raise TraceError(f"unsupported STVI version {version}")
        self.width = fields[2]
        self.height = fields[3]
        self.tiles_x = fields[4]
        self.tiles_y = fields[5]
        self.cb_entries = fields[6]
        self.feat_len = fields[7]
        self.y_coeffs = fields[8]
        self.chroma_coeffs = fields[9]
        self.fps = fields[10]
        self.frame_count = fields[11]
        self.alpha = fields[12]
        self.chroma_weight = fields[13]
        self.gamma = fields[14]
        index_off = fields[15]
        if index_off == 0:
            raise TraceError("trace is incomplete (encode did not finish)")
        self.ncoeff = self.y_coeffs + 2 * self.chroma_coeffs
        if self.ncoeff > self.feat_len:
            raise TraceError("feature header is inconsistent")
        self.tiles_n = self.tiles_x * self.tiles_y
        self.names = coeff_names(self.y_coeffs, self.chroma_coeffs, self.feat_len)
        self.fp.seek(index_off)
        off_raw = self.fp.read(8 * self.frame_count)
        if len(off_raw) != 8 * self.frame_count:
            raise TraceError("truncated frame index")
        self.offsets = struct.unpack(f"<{self.frame_count}Q", off_raw) if self.frame_count else ()
        nsnap_raw = self.fp.read(4)
        if len(nsnap_raw) != 4:
            raise TraceError("truncated snapshot index")
        nsnap = struct.unpack("<I", nsnap_raw)[0]
        snap_raw = self.fp.read(4 * nsnap)
        if len(snap_raw) != 4 * nsnap:
            raise TraceError("truncated snapshot index")
        self.snapshots = list(struct.unpack(f"<{nsnap}I", snap_raw)) if nsnap else []
        if self.frame_count and (not self.snapshots or self.snapshots[0] != 0):
            raise TraceError("trace is missing the frame-0 codebook snapshot")
        self._seg_pal: dict[int, np.ndarray] | None = None
        self._seg_vga: dict[int, np.ndarray] = {}
        self._subset: dict[int, np.ndarray] = {}
        self.next_segment: dict[int, int] = {}

    def close(self) -> None:
        self.fp.close()

    def ensure_segments(self) -> None:
        """Map each segment to its 16-color palette, and each segment to the next one."""
        if self._seg_pal is not None:
            return
        pal: dict[int, np.ndarray] = {}
        vga: dict[int, np.ndarray] = {}
        nxt: dict[int, int] = {}
        prev: int | None = None
        for i in range(self.frame_count):
            self.fp.seek(self.offsets[i])
            _frame, segment, nwin, _nrep, _flags = struct.unpack("<I4H", self._read(12))
            ste = np.frombuffer(self._read(48), dtype=np.uint8).reshape(16, 3).copy()
            segment = int(segment)
            if segment not in pal:
                pal[segment] = ste
            if nwin and segment not in vga:
                self.fp.seek(4, 1)
                vga[segment] = np.frombuffer(self._read(768), dtype=np.uint8).copy()
            if prev is not None and segment != prev and prev not in nxt:
                nxt[prev] = segment
            prev = segment
        self._seg_pal = pal
        self._seg_vga = vga
        self._subset = {seg: subset_from_ste(pal[seg], vga[seg]) for seg in vga}
        self.next_segment = nxt

    def segment_palette(self, segment: int) -> np.ndarray:
        """(16, 3) RGB8 of one segment's STE palette."""
        self.ensure_segments()
        assert self._seg_pal is not None
        return self._seg_pal[segment]

    def feats_for_pens(self, pens64: np.ndarray, segment: int) -> np.ndarray:
        """Features of dithered pens under that segment's VGA palette.

        A next-epoch install is stored with features from the palette that was
        current at install time. The pens themselves belong to ``segment``.
        """
        self.ensure_segments()
        subset = self._subset[segment]
        idx = subset[np.asarray(pens64, dtype=np.uint8)]
        feat = feats_from_indices(
            idx,
            self._seg_vga[segment],
            y_coeffs=self.y_coeffs,
            chroma_coeffs=self.chroma_coeffs,
            alpha=self.alpha,
            chroma_weight=self.chroma_weight,
            gamma=self.gamma,
        )
        if feat.shape[1] == self.feat_len:
            return feat
        out = np.zeros((feat.shape[0], self.feat_len), dtype=np.float32)
        n = min(self.feat_len, feat.shape[1])
        out[:, :n] = feat[:, :n]
        return out

    def read_frame(self, index: int, pixels: bool = True) -> Frame:
        if index < 0 or index >= self.frame_count:
            raise TraceError(f"frame {index} out of range")
        self.fp.seek(self.offsets[index])
        head = self.fp.read(12)
        if len(head) != 12:
            raise TraceError(f"truncated frame {index}")
        frame_index, segment, nwin, nrep, flags = struct.unpack("<I4H", head)
        ste = np.frombuffer(self._read(48), dtype=np.uint8).reshape(16, 3).copy()
        wins: list[WinImage] = []
        src_bytes = self.tiles_n * 64
        for _ in range(nwin):
            seg, _pad = struct.unpack("<HH", self._read(4))
            pal = np.frombuffer(self._read(768), dtype=np.uint8).copy()
            if pixels:
                src = np.frombuffer(self._read(src_bytes), dtype=np.uint8).reshape(self.tiles_n, 64).copy()
            else:
                self.fp.seek(src_bytes, 1)
                src = None
            wins.append(WinImage(seg, pal, src))
        recon_bytes = self.tiles_n * 32
        if pixels:
            recon = np.frombuffer(self._read(recon_bytes), dtype=np.uint8).reshape(self.tiles_n, 32).copy()
        else:
            self.fp.seek(recon_bytes, 1)
            recon = None
        full = None
        if flags & STVI_F_FULL:
            valid = np.frombuffer(self._read(self.cb_entries), dtype=np.uint8).copy()
            tiles = np.frombuffer(self._read(self.cb_entries * 32), dtype=np.uint8).reshape(self.cb_entries, 32).copy()
            feat_bytes = self.cb_entries * self.feat_len * 4
            feats = np.frombuffer(self._read(feat_bytes), dtype="<f4").reshape(self.cb_entries, self.feat_len).copy()
            full = (valid, tiles, feats)
        reps: list[Replace] = []
        feat_bytes = self.feat_len * 4
        for _ in range(nrep):
            index_b, drawable, _pad = struct.unpack("<HBB", self._read(4))
            tile = np.frombuffer(self._read(32), dtype=np.uint8).copy()
            feat = np.frombuffer(self._read(feat_bytes), dtype="<f4").copy()
            reps.append(Replace(index_b, drawable, tile, feat))
        cb_index, skip, dist, dist_n2, tile_feats = self._read_tiles()
        if flags & STVI_F_WIN_RESIDUAL:
            pool = np.frombuffer(self._read(nwin), dtype=np.uint8).copy()
            nbytes = 4 * nwin * self.tiles_n
            if pixels:
                residual = np.frombuffer(self._read(nbytes), dtype="<u4").reshape(nwin, self.tiles_n).copy()
            else:
                self.fp.seek(nbytes, 1)
                residual = None
            for i, win in enumerate(wins):
                win.pool = int(pool[i])
                win.residual = None if residual is None else residual[i]
        nscore = None
        util_events: list[UtilEvent] = []
        if flags & STVI_F_UTIL:
            nscore, nevent = struct.unpack("<HH", self._read(4))
            for _ in range(nevent):
                pass_i, cb, wi = struct.unpack("<HHI", self._read(8))
                predicted, actual = struct.unpack("<qq", self._read(16))
                util_events.append(
                    UtilEvent(pass_i, None if cb == STVI_CB_NONE else cb, wi, predicted, actual)
                )
        return Frame(
            frame_index, segment, flags, ste, wins, recon, full, reps,
            cb_index, skip, dist, dist_n2, tile_feats, nscore, util_events,
        )

    def _read(self, n: int) -> bytes:
        buf = self.fp.read(n)
        if len(buf) != n:
            raise TraceError("truncated trace record")
        return buf

    def _read_tiles(self) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
        stride = 12 + self.feat_len * 4
        raw = np.frombuffer(self._read(stride * self.tiles_n), dtype=np.uint8).reshape(self.tiles_n, stride)
        cb_index = np.ascontiguousarray(raw[:, 0:2]).view("<u2").reshape(-1).copy()
        skip = raw[:, 2].copy()
        dist = np.ascontiguousarray(raw[:, 4:8]).view("<u4").reshape(-1).copy()
        dist_n2 = np.ascontiguousarray(raw[:, 8:12]).view("<u4").reshape(-1).copy()
        feats = np.ascontiguousarray(raw[:, 12:]).view("<f4").reshape(self.tiles_n, self.feat_len).copy()
        return cb_index, skip, dist, dist_n2, feats


class Codebook:
    """Codebook after a given frame, rebuilt from the last full snapshot plus replaces."""

    def __init__(self, trace: Trace):
        self.trace = trace
        n = trace.cb_entries
        self.tiles = np.zeros((n, 32), dtype=np.uint8)
        self.feats = np.zeros((n, trace.feat_len), dtype=np.float32)
        self.valid = np.zeros(n, dtype=np.uint8)
        # Segment that owns each slot's pens. -1 until the first install.
        self.owner = np.full(n, -1, dtype=np.int32)
        self.frame: int | None = None
        self._owner_frame: int | None = None
        self._owner_ckpt: dict[int, np.ndarray] = {}
        self._disp: np.ndarray | None = None
        self._disp_key: tuple[int, int] | None = None

    def at(self, frame: int) -> None:
        if self.frame == frame:
            return
        self.trace.ensure_segments()
        snaps = [s for s in self.trace.snapshots if s <= frame]
        if not snaps:
            raise TraceError(f"no codebook snapshot at or before frame {frame}")
        snap = snaps[-1]
        if self.frame is not None and frame == self.frame + 1 and snap <= self.frame:
            fr = self.trace.read_frame(frame, pixels=False)
            self._apply(fr)
            self._checkpoint_owner(frame)
            self.frame = frame
            return
        full = self.trace.read_frame(snap, pixels=False).full
        if full is None:
            raise TraceError(f"frame {snap} is indexed as a snapshot but has no codebook")
        self.valid, self.tiles, self.feats = full
        for f in range(snap + 1, frame + 1):
            self._apply(self.trace.read_frame(f, pixels=False))
        self.frame = frame
        self._sync_owner(frame)

    def display_feats(self, segment: int) -> np.ndarray:
        """Features matched to the colors on screen.

        Slots owned by ``segment`` keep the stored features. A slot harvested
        for another segment is measured again under that segment's palette,
        because the trace recorded it under the palette that was current then.
        """
        self.trace.ensure_segments()
        key = (self.frame if self.frame is not None else -1, int(segment))
        if self._disp_key == key and self._disp is not None:
            return self._disp
        out = np.array(self.feats, copy=True)
        wrong = (self.owner >= 0) & (self.owner != int(segment))
        if np.any(wrong):
            pens = unpack_pens(self.tiles[wrong]).reshape(-1, 64)
            owners = self.owner[wrong]
            rows = np.flatnonzero(wrong)
            for seg in np.unique(owners):
                seg = int(seg)
                if seg not in self.trace._subset:
                    continue
                take = owners == seg
                out[rows[take]] = self.trace.feats_for_pens(pens[take], seg)
        self._disp = out
        self._disp_key = key
        return out

    def palette_of(self, index: int) -> np.ndarray | None:
        """(16, 3) RGB8 for one slot, or None if that slot was never installed."""
        seg = int(self.owner[index])
        if seg < 0:
            return None
        return self.trace.segment_palette(seg)

    def color_tiles(self) -> np.ndarray:
        """(N, 8, 8, 3) RGB. Each slot uses its segment palette. Stale slots are dimmed."""
        pens = unpack_pens(self.tiles)
        rgb = np.full(pens.shape + (3,), 22, dtype=np.uint8)
        owned = self.owner >= 0
        for seg in np.unique(self.owner[owned]):
            pal = self.trace.segment_palette(int(seg))
            mask = self.owner == seg
            rgb[mask] = pal[pens[mask]]
        dim = owned & (self.valid == 0)
        if np.any(dim):
            rgb[dim] = (rgb[dim].astype(np.uint16) * 40 // 100).astype(np.uint8)
        return rgb

    def _apply(self, fr: Frame) -> None:
        seg = fr.segment
        nxt = self.trace.next_segment.get(seg, -1)
        for rep in fr.reps:
            self.tiles[rep.index] = rep.tile
            self.feats[rep.index] = rep.feat
            self.valid[rep.index] = np.uint8(rep.drawable)
            self.owner[rep.index] = seg if rep.drawable else nxt

    def _checkpoint_owner(self, frame: int) -> None:
        self._owner_frame = frame
        if frame in self.trace.snapshots:
            self._owner_ckpt[frame] = self.owner.copy()

    def _sync_owner(self, frame: int) -> None:
        """Replay installs so each slot remembers the segment whose palette it uses."""
        if self._owner_frame == frame:
            return
        if self._owner_frame is not None and frame > self._owner_frame:
            start = self._owner_frame + 1
        else:
            ckpt = max((s for s in self._owner_ckpt if s <= frame), default=None)
            if ckpt is None:
                self.owner.fill(-1)
                start = 0
            else:
                self.owner[:] = self._owner_ckpt[ckpt]
                start = ckpt + 1
        for f in range(start, frame + 1):
            self._stamp_owner(self.trace.read_frame(f, pixels=False))
            if f in self.trace.snapshots:
                self._owner_ckpt[f] = self.owner.copy()
        self._owner_frame = frame

    def _stamp_owner(self, fr: Frame) -> None:
        seg = fr.segment
        nxt = self.trace.next_segment.get(seg, -1)
        for rep in fr.reps:
            self.owner[rep.index] = seg if rep.drawable else nxt
