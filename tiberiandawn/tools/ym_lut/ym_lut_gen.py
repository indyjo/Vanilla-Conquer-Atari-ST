#!/usr/bin/env python3
"""Generate YM2149 digi LUTs as movep-ready C/asm includes."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
if str(_HERE) not in sys.path:
    sys.path.insert(0, str(_HERE))

from ym_amp import make_amp_fn
from ym_emit import entry_size, write_outputs
from ym_optimal import build_grid_lut, build_optimal_lut, build_remap


def main(argv=None) -> int:
    p = argparse.ArgumentParser(
        description="Generate YM digi LUTs (grid = Digi_Submit quantize; thin = legacy remap)."
    )
    p.add_argument(
        "--mode",
        choices=("grid", "thin"),
        default="grid",
        help="grid: N uniform linear bins (no remap). thin: legacy thin+remap",
    )
    p.add_argument(
        "--model",
        choices=("table", "math", "linear"),
        default="table",
        help="Hatari amplitude model (default: table)",
    )
    p.add_argument(
        "--channels",
        type=int,
        choices=(1, 2, 3),
        default=2,
        help="YM channels used for digi (unused = vol 0)",
    )
    p.add_argument(
        "--bits",
        type=int,
        choices=(5, 6, 7, 8),
        default=6,
        help="LUT depth: 8=256, 7=128, 6=64, 5=32 entries (grid default 6)",
    )
    p.add_argument(
        "--pcm",
        choices=("signed", "unsigned"),
        default="signed",
        help="signed: silence→mid amp (grid + thin remap); unsigned: 0=quiet",
    )
    p.add_argument(
        "--out-dir",
        type=Path,
        default=Path("."),
        help="Directory for outputs",
    )
    p.add_argument(
        "--prefix",
        default=None,
        help="Symbol prefix",
    )
    p.add_argument(
        "--asm",
        action="store_true",
        help="Also write PREFIX_lut.S.inc (.long lines for audio_timer_dac_ym.S)",
    )
    args = p.parse_args(argv)

    n = 1 << args.bits
    prefix = args.prefix or (
        f"ym_grid_{args.channels}ch_{args.bits}bit"
        if args.mode == "grid"
        else f"ym_{args.model}_{args.channels}ch_{args.bits}bit"
    )
    amp_fn = make_amp_fn(args.model)

    if args.mode == "grid":
        entries, amin, amax, stats = build_grid_lut(amp_fn, args.channels, n=n, pcm=args.pcm)
        remap = None
        write_remap = False
    else:
        entries, amin, amax, stats = build_optimal_lut(
            amp_fn, args.channels, args.bits
        )
        remap = build_remap(
            entries,
            amin,
            amax,
            pcm=args.pcm,
            as_byte_offsets=True,
            entry_bytes=entry_size(args.channels),
        )
        write_remap = True

    write_outputs(
        args.out_dir,
        prefix,
        entries,
        remap,
        args.channels,
        write_remap=write_remap,
        write_asm=args.asm,
        model=args.model,
        bits=args.bits,
        pcm=args.pcm,
        amin=amin,
        amax=amax,
        stats=stats,
    )

    es = entry_size(args.channels)
    print(
        f"{prefix}: mode={args.mode} model={args.model} channels={args.channels} "
        f"bits={args.bits}"
    )
    print(
        f"  N={stats['n']} entry_size={es} distinct={stats['distinct']} "
        f"RMSE={stats['rmse']:.2f} ({100.0 * stats['rmse_fs']:.3f}% FS)"
    )
    print(f"  wrote {args.out_dir / (prefix + '_lut.inc')}")
    if write_remap:
        print(f"  wrote {args.out_dir / (prefix + '_remap.inc')}")
    print(f"  wrote {args.out_dir / (prefix + '_meta.inc')}")
    if args.asm:
        print(f"  wrote {args.out_dir / (prefix + '_lut.S.inc')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
