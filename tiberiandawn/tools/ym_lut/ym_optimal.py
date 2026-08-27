"""Identity-optimal YM digi LUTs and least-RMS thinning."""

from __future__ import annotations

import math
from typing import List, Optional, Sequence, Tuple

from ym_amp import AmpFn, VolABC, best_rep_for_amp, enumerate_volumes

LutEntry = Tuple[VolABC, float]  # ((A,B,C), amp)


def linear_targets(amin: float, amax: float, n: int = 256) -> List[float]:
    if n < 2:
        raise ValueError("n must be >= 2")
    span = amax - amin
    return [amin + span * i / (n - 1) for i in range(n)]


def build_monotone_lut(
    amp_fn: AmpFn,
    channels: int,
    level_amps: Optional[Sequence[float]] = None,
    n: int = 256,
) -> Tuple[List[LutEntry], float, float]:
    """
    Build n-entry LUT: monotone non-decreasing amp nearest to linear ramp.

    If level_amps is None, use all achievable amplitudes for `channels`.
    Returns (entries, amin, amax) where amin/amax are the full mode range
    used for the ideal ramp (always full channel-mode range).
    """
    cands = enumerate_volumes(channels)
    rep = best_rep_for_amp(amp_fn, cands)
    all_amps = sorted(rep.keys())
    amin, amax = all_amps[0], all_amps[-1]

    if level_amps is None:
        uniq = all_amps
    else:
        uniq = sorted(set(float(x) for x in level_amps))
        if not uniq:
            raise ValueError("empty level set")
        # Ensure we can still hit silence/full if present in mode
        if amin not in uniq:
            uniq = [amin] + uniq
        if amax not in uniq:
            uniq = uniq + [amax]
        uniq = sorted(set(uniq))

    targets = linear_targets(amin, amax, n)
    entries: List[LutEntry] = []
    prev = -1.0
    for tgt in targets:
        # candidates with amp >= prev (monotone)
        ok = [u for u in uniq if u >= prev - 1e-12]
        if not ok:
            ok = uniq
        best = min(ok, key=lambda u: (abs(u - tgt), u))
        # Prefer rep from full candidate set
        vols = rep.get(best)
        if vols is None:
            # level from thinning may match a key; find nearest rep key
            nearest_key = min(rep.keys(), key=lambda k: abs(k - best))
            vols = rep[nearest_key]
            best = nearest_key
        entries.append((vols, best))
        prev = best
    return entries, amin, amax


def lut_rmse(entries: Sequence[LutEntry], amin: float, amax: float) -> float:
    targets = linear_targets(amin, amax, len(entries))
    err2 = 0.0
    for (_, amp), tgt in zip(entries, targets):
        d = amp - tgt
        err2 += d * d
    return math.sqrt(err2 / len(entries))


def distinct_amps(entries: Sequence[LutEntry]) -> List[float]:
    return sorted({amp for _, amp in entries})


def _proj_rmse(levels: Sequence[float], amin: float, amax: float, n: int = 256) -> float:
    """RMSE of monotone nearest projection of linear ramp onto sorted levels."""
    uniq = sorted(levels)
    targets = linear_targets(amin, amax, n)
    err2 = 0.0
    prev = -1.0
    j0 = 0
    m = len(uniq)
    for tgt in targets:
        while j0 < m and uniq[j0] < prev - 1e-12:
            j0 += 1
        if j0 >= m:
            j0 = m - 1
        best = uniq[j0]
        best_d = abs(best - tgt)
        for j in range(j0 + 1, m):
            d = abs(uniq[j] - tgt)
            if d < best_d:
                best_d = d
                best = uniq[j]
            elif uniq[j] > tgt and d > best_d:
                break
        err2 += (best - tgt) * (best - tgt)
        prev = best
    return math.sqrt(err2 / n)


def thin_levels_greedy(
    amp_fn: AmpFn,
    channels: int,
    levels: Sequence[float],
    keep: int,
) -> List[float]:
    """
    Greedily drop levels (except endpoints) that least increase RMSE of a
    256-slot monotone projection vs the linear ramp, until `keep` levels remain.
    """
    del amp_fn, channels  # RMSE depends only on amplitude values
    remain = sorted(set(float(x) for x in levels))
    if keep < 2:
        raise ValueError("keep must be >= 2")
    if len(remain) <= keep:
        return remain

    amin, amax = remain[0], remain[-1]

    while len(remain) > keep:
        best_drop = None
        best_r = None
        for i, cand in enumerate(remain):
            if cand == amin or cand == amax:
                continue
            trial = remain[:i] + remain[i + 1 :]
            r = _proj_rmse(trial, amin, amax)
            if best_r is None or r < best_r - 1e-15:
                best_r = r
                best_drop = cand
        if best_drop is None:
            break
        remain.remove(best_drop)
    return remain


def build_lut_from_levels(
    amp_fn: AmpFn,
    channels: int,
    level_amps: Sequence[float],
) -> Tuple[List[LutEntry], float, float]:
    """One LUT entry per level (sorted), using best (A,B,C) for each amp."""
    cands = enumerate_volumes(channels)
    rep = best_rep_for_amp(amp_fn, cands)
    all_amps = sorted(rep.keys())
    amin, amax = all_amps[0], all_amps[-1]
    uniq = sorted(set(float(x) for x in level_amps))
    entries: List[LutEntry] = []
    for amp in uniq:
        vols = rep.get(amp)
        if vols is None:
            nearest_key = min(rep.keys(), key=lambda k: abs(k - amp))
            vols = rep[nearest_key]
            amp = nearest_key
        entries.append((vols, amp))
    return entries, amin, amax


def build_grid_lut(
    amp_fn: AmpFn,
    channels: int,
    n: int = 64,
    pcm: str = "signed",
) -> Tuple[List[LutEntry], float, float, dict]:
    """
    n-entry LUT for Digi_Submit index = sample & mask (n=64 → andi #$FC).

    Slot i is the representative sample byte s = i * (256/n).
    pcm=unsigned: target amp rises with s (0 = quiet).
    pcm=signed: target amp uses u = s ^ 0x80 so signed silence (s=0) is mid
    amplitude — matching STE-style signed PCM without abs/fold.
    """
    if pcm not in ("signed", "unsigned"):
        raise ValueError("pcm must be signed|unsigned")
    if n < 2 or (256 % n) != 0:
        raise ValueError("n must divide 256 and be >= 2")
    step = 256 // n

    cands = enumerate_volumes(channels)
    rep = best_rep_for_amp(amp_fn, cands)
    uniq = sorted(rep.keys())
    amin, amax = uniq[0], uniq[-1]
    span = amax - amin

    entries: List[LutEntry] = []
    targets: List[float] = []
    for i in range(n):
        s = i * step
        u = (s ^ 0x80) if pcm == "signed" else s
        tgt = amin + span * u / 255.0
        targets.append(tgt)
        best = min(uniq, key=lambda a: (abs(a - tgt), a))
        entries.append((rep[best], best))

    err2 = sum((amp - tgt) * (amp - tgt) for (_, amp), tgt in zip(entries, targets))
    rmse = math.sqrt(err2 / n)
    stats = {
        "n": n,
        "distinct_full": len(distinct_amps(entries)),
        "distinct": len(distinct_amps(entries)),
        "rmse": rmse,
        "rmse_fs": rmse / span if span else 0.0,
        "thinned": False,
        "mode": "grid",
        "pcm": pcm,
        "requested_n": n,
    }
    return entries, amin, amax, stats


def build_optimal_lut(
    amp_fn: AmpFn,
    channels: int,
    bits: int,
) -> Tuple[List[LutEntry], float, float, dict]:
    """
    bits=8 -> 256 entries (monotone nearest to linear ramp).
    bits=7/6/5 -> least-RMS subset of that table with 128/64/32 levels;
    LUT is exactly those levels (sorted); remap maps 8-bit samples onto them.
    """
    if bits not in (5, 6, 7, 8):
        raise ValueError("bits must be 5, 6, 7, or 8")
    n = 1 << bits

    full, amin, amax = build_monotone_lut(amp_fn, channels, None, n=256)
    full_levels = distinct_amps(full)
    full_rmse = lut_rmse(full, amin, amax)

    if bits == 8:
        stats = {
            "n": 256,
            "distinct_full": len(full_levels),
            "distinct": len(full_levels),
            "rmse": full_rmse,
            "rmse_fs": full_rmse / (amax - amin) if amax > amin else 0.0,
            "thinned": False,
        }
        return full, amin, amax, stats

    if len(full_levels) <= n:
        entries, _, _ = build_lut_from_levels(amp_fn, channels, full_levels)
        rmse = _proj_rmse(full_levels, amin, amax)
        stats = {
            "n": len(entries),
            "distinct_full": len(full_levels),
            "distinct": len(entries),
            "rmse": rmse,
            "rmse_fs": rmse / (amax - amin) if amax > amin else 0.0,
            "thinned": False,
            "requested_n": n,
        }
        return entries, amin, amax, stats

    kept = thin_levels_greedy(amp_fn, channels, full_levels, keep=n)
    entries, _, _ = build_lut_from_levels(amp_fn, channels, kept)
    rmse = _proj_rmse(kept, amin, amax)
    stats = {
        "n": len(entries),
        "distinct_full": len(full_levels),
        "distinct": len(entries),
        "rmse": rmse,
        "rmse_fs": rmse / (amax - amin) if amax > amin else 0.0,
        "thinned": True,
        "levels_kept": len(kept),
        "requested_n": n,
    }
    return entries, amin, amax, stats


def build_remap(
    entries: Sequence[LutEntry],
    amin: float,
    amax: float,
    pcm: str = "signed",
    *,
    as_byte_offsets: bool = True,
    entry_bytes: int = 4,
) -> List[int]:
    """
    Map each 8-bit sample to a LUT entry index, or (default) to a byte offset
    index*entry_bytes for ISR adda.w without a shift (requires max offset <= 255).
    signed: u = s ^ 0x80 (STE); unsigned: u = s.
    """
    if pcm not in ("signed", "unsigned"):
        raise ValueError("pcm must be signed|unsigned")
    n = len(entries)
    amps = [amp for _, amp in entries]
    remap = [0] * 256
    span = amax - amin
    for s in range(256):
        u = (s ^ 0x80) if pcm == "signed" else s
        ideal = amin + span * u / 255.0
        best_j = 0
        best_d = abs(amps[0] - ideal)
        for j in range(1, n):
            d = abs(amps[j] - ideal)
            if d < best_d:
                best_d = d
                best_j = j
        if as_byte_offsets:
            off = best_j * entry_bytes
            if off > 255:
                raise ValueError(
                    f"byte offset {off} > 255 (n={n} entry_bytes={entry_bytes}); "
                    "use indices or fewer/smaller entries"
                )
            remap[s] = off
        else:
            remap[s] = best_j
    return remap
