"""Hatari-parity YM2149 amplitude models for 4-bit digi volumes."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Callable, List, Sequence, Tuple

DATA_DIR = Path(__file__).resolve().parent / "data"

# Hatari sound.c: map fixed 4-bit volume -> 5-bit table index
YM_VOLUME_4TO5: Tuple[int, ...] = (
    0, 1, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31
)

VolABC = Tuple[int, int, int]  # (A, B, C) each 0..15
AmpFn = Callable[[int, int, int], float]


def _load_json(name: str):
    with open(DATA_DIR / name, "r", encoding="utf-8") as f:
        return json.load(f)


def load_paulo_table() -> List[List[List[int]]]:
    """volume_table[C][B][A] from Paulo Simoes / Hatari ym2149_fixed_vol.h."""
    table = _load_json("ym2149_fixed_vol.json")
    if len(table) != 16 or len(table[0]) != 16 or len(table[0][0]) != 16:
        raise ValueError("ym2149_fixed_vol.json must be 16x16x16")
    return table


def load_ymout1c5bit() -> List[int]:
    vals = _load_json("ymout1c5bit.json")
    if len(vals) != 32:
        raise ValueError("ymout1c5bit.json must have 32 entries")
    return vals


def build_math_conductance() -> List[float]:
    """Savinkoff AC+DC model conductance_[0..31] from Hatari sound.c."""
    max_vol = 65535.0
    fourth2 = 1.19
    warp = 1.666666666666666667
    _ = max_vol  # used only in amp formula
    conductance = 2.0 / 3.0 / (1.0 - 1.0 / warp) - 2.0 / 3.0
    conductance_ = [0.0] * 32
    for i in range(31, 0, -1):
        conductance_[i] = conductance / 2.0
        conductance = (
            1.0 / (1.0 - 1.0 / fourth2 / (1.0 / conductance + 1.0)) - 1.0
        )
    conductance_[0] = 1.0e-8
    return conductance_


def math_amp_5bit(conductance_: Sequence[float], i: int, j: int, k: int) -> float:
    """Hatari YM2149_BuildModelVolumeTable cell (i,j,k) each 0..31."""
    max_vol = 65535.0
    warp = 1.666666666666666667
    return 0.5 + (max_vol * warp) / (
        1.0 + 1.0 / (conductance_[i] + conductance_[j] + conductance_[k])
    )


def make_amp_fn(model: str) -> AmpFn:
    """
    Return amp(A, B, C) for 4-bit volumes using Hatari model name:
    table | math | linear
    """
    model = model.lower()
    if model == "table":
        table = load_paulo_table()

        def amp(a: int, b: int, c: int) -> float:
            return float(table[c][b][a])

        return amp

    if model == "math":
        g = build_math_conductance()

        def amp(a: int, b: int, c: int) -> float:
            return math_amp_5bit(
                g, YM_VOLUME_4TO5[c], YM_VOLUME_4TO5[b], YM_VOLUME_4TO5[a]
            )

        return amp

    if model == "linear":
        ymout = load_ymout1c5bit()

        def amp(a: int, b: int, c: int) -> float:
            # Hatari mean of three 5-bit levels (same as linear cube at 4to5 indices)
            return (
                ymout[YM_VOLUME_4TO5[a]]
                + ymout[YM_VOLUME_4TO5[b]]
                + ymout[YM_VOLUME_4TO5[c]]
            ) / 3.0

        return amp

    raise ValueError(f"unknown model {model!r}; use table|math|linear")


def enumerate_volumes(channels: int) -> List[VolABC]:
    """All volume tuples for 1/2/3 digi channels (unused = 0)."""
    if channels not in (1, 2, 3):
        raise ValueError("channels must be 1, 2, or 3")
    out: List[VolABC] = []
    if channels == 1:
        for a in range(16):
            out.append((a, 0, 0))
    elif channels == 2:
        for a in range(16):
            for b in range(16):
                out.append((a, b, 0))
    else:
        for a in range(16):
            for b in range(16):
                for c in range(16):
                    out.append((a, b, c))
    return out


def tie_key(vols: VolABC) -> Tuple[int, int, int, int]:
    """Deterministic preference: A>=B>=C, then larger sum, then A,B,C."""
    a, b, c = vols
    ordered = 1 if a >= b >= c else 0
    return (ordered, a + b + c, a, b)


def best_rep_for_amp(
    amp_fn: AmpFn, candidates: Sequence[VolABC]
) -> dict:
    """Map unique amplitude -> best (A,B,C) representative."""
    rep = {}
    for vols in candidates:
        a, b, c = vols
        amp = amp_fn(a, b, c)
        key = amp  # float identity for measured ints; math may need rounding
        # Use exact float as key; for table ints this is fine
        prev = rep.get(key)
        if prev is None or tie_key(vols) > tie_key(prev):
            rep[key] = vols
    return rep


def amp_range(amp_fn: AmpFn, channels: int) -> Tuple[float, float]:
    cands = enumerate_volumes(channels)
    amps = [amp_fn(a, b, c) for a, b, c in cands]
    return min(amps), max(amps)
