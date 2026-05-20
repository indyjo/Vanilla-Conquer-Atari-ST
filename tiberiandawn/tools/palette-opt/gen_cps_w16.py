#!/usr/bin/env python3
"""
Generate .W16 C2P weight bundles for C&C CPS screens and map-selection context palettes.

Run from TIBERIANDAWN (requires built palette-opt):
  python3 tools/palette-opt/gen_cps_w16.py [--mix PATH] [--out DIR]

Embedded-palette CPS (TITLE, ATTRACT2): extract 768-byte PAL from skip header, then
palette-opt -p tmp.pal --dump NAME.W16 (default VGA subset 0..15, same as HTITLE).

External palette (SATSEL.PAL): --subset-spread.

Map CLICK_* overlays use WSA context weights (EUROPE.W16, …); this script also
refreshes those from loose .WSA next to the mix when present.
"""
from __future__ import annotations

import argparse
import struct
import subprocess
import sys
from pathlib import Path


def mix_crc(name: str) -> int:
    """Westwood MIX entry key (matches ATARILIB/misc.cpp Calculate_CRC)."""
    data = name.upper().encode("ascii")
    length = len(data)
    crc = 0
    chunks = (length + 3) // 4
    for i in range(chunks):
        chunk = data[i * 4 : i * 4 + 4]
        value = int.from_bytes(chunk.ljust(4, b"\x00"), "little")
        high_bit = 1 if (crc & 0x80000000) else 0
        crc = ((crc << 1) | high_bit) & 0xFFFFFFFF
        crc = (crc + value) & 0xFFFFFFFF
    if crc >= 0x80000000:
        return crc - 0x100000000
    return crc


def mix_extract(mix_path: Path, entry: str) -> bytes | None:
    key = mix_crc(entry)
    blob = mix_path.read_bytes()
    if len(blob) < 6:
        return None
    count, data_size = struct.unpack_from("<h", blob, 0)[0], struct.unpack_from("<l", blob, 2)[0]
    index_bytes = count * 12
    data_base = 6 + index_bytes
    blocks = []
    for i in range(count):
        off = 6 + i * 12
        crc, offset, size = struct.unpack_from("<lll", blob, off)
        blocks.append((crc, offset, size))
    blocks.sort(key=lambda t: t[0])
    lo, hi = 0, len(blocks) - 1
    while lo <= hi:
        mid = (lo + hi) // 2
        if blocks[mid][0] < key:
            lo = mid + 1
        elif blocks[mid][0] > key:
            hi = mid - 1
        else:
            _crc, offset, size = blocks[mid]
            if offset < 0 or size <= 0 or offset > data_size or size > data_size - offset:
                return None
            return blob[data_base + offset : data_base + offset + size]
    return None


def cps_embedded_palette(cps: bytes) -> bytes | None:
    if len(cps) < 10 + 768:
        return None
    _file_size, method, pad, size, skip = struct.unpack_from("<HbbLi", cps, 0)
    if skip != 768:
        return None
    return cps[10 : 10 + 768]


def run_palette_opt(po: Path, palette: Path, out_w16: Path, subset_spread: bool) -> None:
    cmd = [str(po)]
    if subset_spread:
        cmd.append("--subset-spread")
    cmd.extend(["-p", str(palette), "--dump", str(out_w16)])
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)


def wsa_to_w16(po: Path, wsa_path: Path, out_w16: Path) -> None:
    subprocess.run(
        [str(po), "-p", str(wsa_path), "--dump", str(out_w16)],
        check=True,
        stdout=subprocess.DEVNULL,
    )


def main() -> int:
    td = Path(__file__).resolve().parents[2]
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--mix",
        type=Path,
        default=td / "bin" / "AtariST" / "CONQUER.MIX",
        help="CONQUER.MIX path (embedded CPS + SATSEL.PAL)",
    )
    ap.add_argument(
        "--out",
        type=Path,
        default=td / "atari-assets",
        help="Output directory for .W16 files",
    )
    args = ap.parse_args()

    po = td / "tools" / "palette-opt" / "palette-opt"
    if not po.is_file():
        print("error: build palette-opt first: make -C tools/palette-opt", file=sys.stderr)
        return 1
    if not args.mix.is_file():
        print(f"error: MIX not found: {args.mix}", file=sys.stderr)
        return 1

    args.out.mkdir(parents=True, exist_ok=True)
    tmp = td / "tools" / "palette-opt" / "_cps_pal_tmp"
    tmp.mkdir(parents=True, exist_ok=True)

    embedded_cps = ("TITLE.CPS", "ATTRACT2.CPS")
    for cps_name in embedded_cps:
        raw = mix_extract(args.mix, cps_name)
        if not raw:
            print(f"skip {cps_name}: not in MIX", file=sys.stderr)
            continue
        pal = cps_embedded_palette(raw)
        if not pal:
            print(f"skip {cps_name}: no 768-byte embedded palette", file=sys.stderr)
            continue
        stem = cps_name.rsplit(".", 1)[0]
        pal_path = tmp / f"{stem}.PAL"
        w16_path = args.out / f"{stem}.W16"
        pal_path.write_bytes(pal)
        run_palette_opt(po, pal_path, w16_path, subset_spread=False)
        print(f"wrote {w16_path.relative_to(td)}")

    pal_raw = mix_extract(args.mix, "SATSEL.PAL")
    if pal_raw and len(pal_raw) == 768:
        pal_path = tmp / "SATSEL.PAL"
        w16_path = args.out / "SATSEL.W16"
        pal_path.write_bytes(pal_raw)
        run_palette_opt(po, pal_path, w16_path, subset_spread=True)
        print(f"wrote {w16_path.relative_to(td)}")
    else:
        print("skip SATSEL.PAL: missing or wrong size", file=sys.stderr)

    wsa_context = (
        "EUROPE.WSA",
        "BOSNIA.WSA",
        "AFRICA.WSA",
        "S_AFRICA.WSA",
    )
    wsa_dir = args.mix.parent
    for wsa_name in wsa_context:
        wsa_path = wsa_dir / wsa_name
        if not wsa_path.is_file():
            print(f"skip {wsa_name}: loose file not beside MIX", file=sys.stderr)
            continue
        stem = wsa_name.rsplit(".", 1)[0]
        w16_path = args.out / f"{stem}.W16"
        wsa_to_w16(po, wsa_path, w16_path)
        print(f"wrote {w16_path.relative_to(td)} (CLICK_* context)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
