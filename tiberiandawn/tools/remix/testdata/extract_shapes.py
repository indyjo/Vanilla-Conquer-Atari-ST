#!/usr/bin/env python3
"""
Extract curated KeyFrame SHP test blobs from an unmodified CONQUER.MIX.

Usage:
  python3 testdata/extract_shapes.py /path/to/conquer.mix
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

# CRC32 of upper-case filename (Westwood MIX index key).
SHAPES = (
    "50CAL.SHP",
    "POWER.SHP",
    "BOMB.SHP",
    "MINIGUN.SHP",
    "OPTIONS.SHP",
    "SMOKE_M.SHP",
    "RADAR.GDI",
    "TREX.SHP",
    "E4.SHP",
    "TRANS.ICN",
)


def calculate_crc(name: str) -> int:
    data = name.upper().encode("ascii")
    crc = 0
    length = len(data)
    num_chunks = (length + 3) >> 2
    for i in range(num_chunks):
        chunk_start = i * 4
        avail = length - chunk_start
        value = (
            (data[chunk_start] if avail > 0 else 0)
            | ((data[chunk_start + 1] << 8) if avail > 1 else 0)
            | ((data[chunk_start + 2] << 16) if avail > 2 else 0)
            | ((data[chunk_start + 3] << 24) if avail > 3 else 0)
        )
        high_bit = 1 if (crc & 0x80000000) else 0
        crc = ((crc << 1) | high_bit) & 0xFFFFFFFF
        crc = (crc + value) & 0xFFFFFFFF
    return crc


def read_mix_entries(path: Path) -> dict[int, tuple[int, int]]:
    data = path.read_bytes()
    if len(data) < 6:
        raise SystemExit(f"{path}: file too small")
    count = struct.unpack_from("<H", data, 0)[0]
    data_base = 6 + count * 12
    entries: dict[int, tuple[int, int]] = {}
    for i in range(count):
        off = 6 + i * 12
        crc, entry_off, size = struct.unpack_from("<III", data, off)
        entries[crc] = (entry_off, size)
    if data_base > len(data):
        raise SystemExit(f"{path}: corrupt index")
    return entries


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} CONQUER.MIX", file=sys.stderr)
        return 2

    mix_path = Path(sys.argv[1])
    out_dir = Path(__file__).resolve().parent / "shapes"
    out_dir.mkdir(parents=True, exist_ok=True)

    entries = read_mix_entries(mix_path)
    mix_data = mix_path.read_bytes()
    count = struct.unpack_from("<H", mix_data, 0)[0]
    data_base = 6 + count * 12

    for name in SHAPES:
        crc = calculate_crc(name)
        if crc not in entries:
            print(f"missing {name} (crc 0x{crc:08X})", file=sys.stderr)
            return 1
        off, size = entries[crc]
        start = data_base + off
        end = start + size
        if end > len(mix_data):
            print(f"truncated {name}", file=sys.stderr)
            return 1
        blob = mix_data[start:end]
        out_path = out_dir / name
        out_path.write_bytes(blob)
        print(f"wrote {out_path.name} ({size} bytes)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
