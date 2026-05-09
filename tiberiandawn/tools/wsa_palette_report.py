#!/usr/bin/env python3
"""
Dump WSA header/palette info and compare palettes against TEMPERAT.PAL.

WSA format details follow WIN32LIB/WSA.CPP:
- Base header is 14 bytes (7 little-endian 16-bit fields).
- The frame offset table has (total_frames + 2) uint32 entries and starts at 14.
- If header.flags bit0 is set, a 768-byte RGB palette follows the offset table.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Sequence, Tuple


@dataclass
class WsaHeader:
    total_frames: int
    pixel_x: int
    pixel_y: int
    pixel_width: int
    pixel_height: int
    largest_frame_size: int
    flags: int


@dataclass
class WsaInfo:
    path: Path
    size: int
    header: WsaHeader
    offset_table_count: int
    offset_table_bytes: int
    palette_present: bool
    palette_file_offset: Optional[int]
    palette: Optional[bytes]


def parse_wsa(path: Path) -> WsaInfo:
    raw = path.read_bytes()
    if len(raw) < 14:
        raise ValueError(f"{path}: file too short for WSA base header")

    total_frames, pixel_x, pixel_y, pixel_width, pixel_height, largest_frame_size, flags = struct.unpack_from(
        "<7H", raw, 0
    )
    header = WsaHeader(
        total_frames=total_frames,
        pixel_x=pixel_x,
        pixel_y=pixel_y,
        pixel_width=pixel_width,
        pixel_height=pixel_height,
        largest_frame_size=largest_frame_size,
        flags=flags,
    )

    # Matches offsets_size = (total_frames + 2) << 2 in WIN32LIB/WSA.CPP.
    table_count = total_frames + 2
    table_bytes = table_count * 4
    table_start = 14
    table_end = table_start + table_bytes
    if table_end > len(raw):
        raise ValueError(
            f"{path}: file too short for offset table "
            f"(need {table_end} bytes, have {len(raw)})"
        )

    palette_present = bool(flags & 0x0001)
    palette_off: Optional[int] = None
    palette: Optional[bytes] = None
    if palette_present:
        palette_off = table_end
        palette_end = palette_off + 768
        if palette_end > len(raw):
            raise ValueError(
                f"{path}: header says palette present, but file too short "
                f"(need {palette_end} bytes, have {len(raw)})"
            )
        palette = raw[palette_off:palette_end]

    return WsaInfo(
        path=path,
        size=len(raw),
        header=header,
        offset_table_count=table_count,
        offset_table_bytes=table_bytes,
        palette_present=palette_present,
        palette_file_offset=palette_off,
        palette=palette,
    )


def load_temperat_palette(explicit_pal_path: Optional[Path], fallback_cpp_path: Optional[Path]) -> Tuple[bytes, str]:
    if explicit_pal_path:
        data = explicit_pal_path.read_bytes()
        if len(data) != 768:
            raise ValueError(f"{explicit_pal_path}: expected 768 bytes, got {len(data)}")
        return data, str(explicit_pal_path)

    if fallback_cpp_path:
        text = fallback_cpp_path.read_text(encoding="utf-8", errors="replace")
        m = re.search(r"kStTemperatPal768\s*\[\s*768\s*\]\s*=\s*\{(.*?)\};", text, flags=re.S)
        if not m:
            raise ValueError(f"{fallback_cpp_path}: couldn't find kStTemperatPal768[768] initializer")
        body = m.group(1)
        toks = re.findall(r"0x[0-9A-Fa-f]+|\d+", body)
        vals = [int(t, 0) for t in toks]
        if len(vals) != 768:
            raise ValueError(f"{fallback_cpp_path}: expected 768 values, got {len(vals)}")
        if any(v < 0 or v > 255 for v in vals):
            raise ValueError(f"{fallback_cpp_path}: palette contains out-of-range value")
        return bytes(vals), str(fallback_cpp_path)

    raise ValueError("No TEMPERAT source provided")


def _build_ranges(indices: Sequence[int]) -> List[Tuple[int, int]]:
    if not indices:
        return []
    out: List[Tuple[int, int]] = []
    s = indices[0]
    e = s
    for i in indices[1:]:
        if i == e + 1:
            e = i
            continue
        out.append((s, e))
        s = i
        e = i
    out.append((s, e))
    return out


def compare_palette_to_ref(pal: bytes, ref: bytes) -> dict:
    if len(pal) != 768 or len(ref) != 768:
        raise ValueError("Palette compare expects 768-byte palettes")

    equal_entries: List[int] = []
    diff_entries: List[int] = []
    per_entry_diffs: List[dict] = []

    for idx in range(256):
        a = pal[idx * 3 : idx * 3 + 3]
        b = ref[idx * 3 : idx * 3 + 3]
        if a == b:
            equal_entries.append(idx)
        else:
            diff_entries.append(idx)
            per_entry_diffs.append(
                {
                    "index": idx,
                    "wsa_rgb": [a[0], a[1], a[2]],
                    "temperat_rgb": [b[0], b[1], b[2]],
                }
            )

    return {
        "matches_all": len(diff_entries) == 0,
        "equal_count": len(equal_entries),
        "diff_count": len(diff_entries),
        "equal_ranges": _build_ranges(equal_entries),
        "diff_ranges": _build_ranges(diff_entries),
        "diff_entries": per_entry_diffs,
    }


def write_palette(path: Path, pal: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(pal)


def format_ranges(ranges: Sequence[Tuple[int, int]]) -> str:
    if not ranges:
        return "(none)"
    return ", ".join(f"{a}" if a == b else f"{a}-{b}" for a, b in ranges)


def main() -> int:
    parser = argparse.ArgumentParser(description="Dump WSA header + palette, compare to TEMPERAT palette")
    parser.add_argument("wsa_files", nargs="+", help="Input WSA files")
    parser.add_argument(
        "--temperat-pal",
        type=Path,
        help="Path to TEMPERAT.PAL (768-byte RGB). If omitted, --temperat-cpp is used.",
    )
    parser.add_argument(
        "--temperat-cpp",
        type=Path,
        default=Path("ATARILIB/st_temperat_palette_data.cpp"),
        help="Fallback C++ palette array source (default: ATARILIB/st_temperat_palette_data.cpp)",
    )
    parser.add_argument(
        "--extract-dir",
        type=Path,
        default=Path("wsa_palette_extract"),
        help="Directory for extracted embedded palettes",
    )
    parser.add_argument(
        "--report-json",
        type=Path,
        default=Path("wsa_palette_report.json"),
        help="Write machine-readable report JSON",
    )
    args = parser.parse_args()

    ref_pal, ref_src = load_temperat_palette(args.temperat_pal, args.temperat_cpp)

    report = {
        "reference_palette_source": ref_src,
        "files": [],
    }

    print(f"Reference palette: {ref_src}")
    print()

    for wsa_name in args.wsa_files:
        wsa_path = Path(wsa_name)
        info = parse_wsa(wsa_path)

        print(f"{wsa_path}")
        print(f"  size: {info.size} bytes")
        print(
            "  header: "
            f"frames={info.header.total_frames}, x={info.header.pixel_x}, y={info.header.pixel_y}, "
            f"w={info.header.pixel_width}, h={info.header.pixel_height}, "
            f"largest_frame_size={info.header.largest_frame_size}, flags=0x{info.header.flags:04X}"
        )
        print(
            "  offset_table: "
            f"{info.offset_table_count} entries ({info.offset_table_bytes} bytes) at file+14"
        )

        item = {
            "file": str(wsa_path),
            "size": info.size,
            "header": info.header.__dict__,
            "offset_table_count": info.offset_table_count,
            "offset_table_bytes": info.offset_table_bytes,
            "palette_present": info.palette_present,
        }

        if not info.palette_present:
            print("  embedded_palette: no")
            item["compare"] = None
            print()
            report["files"].append(item)
            continue

        assert info.palette is not None
        assert info.palette_file_offset is not None
        pal_out = args.extract_dir / (wsa_path.name + ".embedded.pal")
        write_palette(pal_out, info.palette)
        comp = compare_palette_to_ref(info.palette, ref_pal)

        print(f"  embedded_palette: yes (768 bytes at file+{info.palette_file_offset})")
        print(f"  extracted_palette: {pal_out}")
        print(f"  compare_to_temperat: {'MATCH' if comp['matches_all'] else 'DIFF'}")
        print(
            f"    equal entries: {comp['equal_count']} / 256 "
            f"(ranges: {format_ranges(comp['equal_ranges'])})"
        )
        print(
            f"    diff entries:  {comp['diff_count']} / 256 "
            f"(ranges: {format_ranges(comp['diff_ranges'])})"
        )
        print()

        item["palette_file_offset"] = info.palette_file_offset
        item["extracted_palette"] = str(pal_out)
        item["compare"] = comp
        report["files"].append(item)

    args.report_json.parent.mkdir(parents=True, exist_ok=True)
    args.report_json.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"JSON report written: {args.report_json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

