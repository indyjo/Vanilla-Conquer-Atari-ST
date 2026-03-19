#!/usr/bin/env python3
"""
Mouse SHP inspector for Westwood/C&C "shape blocks" (classic ShapeBlock_Type).

This targets the format used by MOUSE.SHP in the C&C Remastered collection:
  - Shape block: [u16 NumShapes][s32 offsets...]
  - Each shape: Shape_Type header + payload

It then attempts a few common decodes for each shape payload:
  - ShapeType == 2: uncompressed bytes (payload is raw WxH pixels)
  - Otherwise:
      1) optional CompHeaderType wrapper (8 bytes: Method/pad/Size/Skip)
      2) raw LCW stream (Format 80) immediately after the Shape_Type header
      3) transparent failures return empty output

Finally it prints an 8x8 pixel hex dump for quick visual confirmation.

By default, prints a full table of every frame: index-table byte position, raw s32
offset, absolute ptr (= 2+off), validity, and quick header fields (matches Extract_Shape).
Use --no-offset-table to skip that listing.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path
from typing import Optional, Tuple


def u16le(b: bytes, off: int) -> int:
    return b[off] | (b[off + 1] << 8)


def s32le(b: bytes, off: int) -> int:
    return struct.unpack_from("<i", b, off)[0]


def u32le(b: bytes, off: int) -> int:
    return struct.unpack_from("<I", b, off)[0]


def u24le(b: bytes, off: int) -> int:
    return b[off] | (b[off + 1] << 8) | (b[off + 2] << 16)


def read_shape_type_header(shape: bytes) -> dict:
    """
    Parse the in-memory `Shape_Type` header for classic shape blocks.

    This should match the offsets used in `WIN32LIB/SRCDEBUG/WWMOUSE.ASM`:
      - ShapeType:      +0 (u16 LE)
      - Height:         +2 (u8)
      - Width:          +3 (u16 LE)
      - OriginalHeight:+5 (u8)
      - ShapeSize:      +6 (u16 LE)
      - DataLength:     +8 (u16 LE)  -- for LCW decompression output length
      - Colortable[16]:+10..+25 (only for MAKESHAPE_COMPACT)
    """
    shape_type = u16le(shape, 0)
    height = shape[2]

    # ASM uses packed offsets: width at +3.
    width_packed = u16le(shape, 3)
    width_aligned = u16le(shape, 4)  # kept for debug only
    original_height = shape[5]
    shape_size = u16le(shape, 6)
    data_length = u16le(shape, 8)

    MAKESHAPE_COMPACT = 0x0001
    MAKESHAPE_NOCOMP = 0x0002
    compact = (shape_type & MAKESHAPE_COMPACT) != 0
    nocomp = (shape_type & MAKESHAPE_NOCOMP) != 0

    # ASM treats header as:
    #   - non-compact: 10 bytes (no colortable)
    #   - compact:     26 bytes (10 + 16-byte colortable)
    header_size = 26 if compact else 10

    need_packed = width_packed * height
    need_aligned = width_aligned * height

    def plausible(w: int, h: int) -> bool:
        return 1 <= w <= 64 and 1 <= h <= 64

    plausible_packed = plausible(width_packed, height)
    plausible_aligned = plausible(width_aligned, height)

    # Prefer packed width; only fall back if it's clearly bogus.
    if plausible_packed:
        width = width_packed
        need = need_packed
    elif plausible_aligned:
        width = width_aligned
        need = need_aligned
    else:
        width = width_packed
        need = need_packed

    return {
        "shape_type": shape_type,
        "height": height,
        "width_packed": width_packed,
        "width_aligned": width_aligned,
        "plausible_packed": plausible_packed,
        "plausible_aligned": plausible_aligned,
        "original_height": original_height,
        "shape_size": shape_size,
        "data_length": data_length,
        "compact": compact,
        "nocomp": nocomp,
        "header_size": header_size,
        "width": width,
        "need": need,  # width*height pixels
    }


def lcw_uncompress(src: bytes, out_len: int) -> Optional[bytes]:
    """
    Westwood LCW (Format 80) decompression, matching the repo's C implementation intent.

    Bounded by out_len.
    Supports "relative mode" if the stream starts with a leading 0 byte.
    """
    if out_len <= 0:
        return None

    i = 0
    relative_mode = False
    if i < len(src) and src[i] == 0:
        relative_mode = True
        i += 1

    out = bytearray()
    dst0 = 0

    def read_u16(off: int) -> int:
        if off + 1 >= len(src):
            return 0
        return src[off] | (src[off + 1] << 8)

    while len(out) < out_len and i < len(src):
        code = src[i]
        i += 1

        # cmd2: 0ccc pppp (+pp)
        if (code & 0x80) == 0:
            if i >= len(src):
                break
            count = ((code & 0x70) >> 4) + 3
            pos = ((code & 0x0F) << 8) | src[i]
            i += 1
            start = len(out) - pos
            if start < dst0:
                break
            for k in range(count):
                if len(out) >= out_len:
                    break
                out.append(out[start + k])
            continue

        # cmd1: literal copy, 0x80 = end marker (count==0)
        if (code & 0x40) == 0:
            count = code & 0x3F
            if count == 0:
                break
            if i + count > len(src):
                break
            out += src[i : i + count]
            i += count
            continue

        # cmd4: fill
        if code == 0xFE:
            if i + 2 >= len(src):
                break
            count = read_u16(i)
            i += 2
            if i >= len(src):
                break
            value = src[i]
            i += 1
            take = min(count, out_len - len(out))
            out += bytes([value]) * take
            continue

        # cmd5: long copy
        if code == 0xFF:
            if i + 3 >= len(src):
                break
            count = read_u16(i)
            i += 2
            posw = read_u16(i)
            i += 2
            if relative_mode:
                start = len(out) - posw
            else:
                start = posw
            if start < dst0:
                break
            for k in range(count):
                if len(out) >= out_len:
                    break
                out.append(out[start + k])
            continue

        # cmd3: medium copy
        count = (code & 0x3F) + 3
        if i + 1 >= len(src):
            break
        posw = read_u16(i)
        i += 2
        if relative_mode:
            start = len(out) - posw
        else:
            start = posw
        if start < dst0:
            break
        for k in range(count):
            if len(out) >= out_len:
                break
            out.append(out[start + k])

    if len(out) != out_len:
        return None
    return bytes(out)


def parse_comp_header(payload: bytes) -> Optional[dict]:
    """
    CompHeaderType layout (8 bytes, little-endian):
      Method: u8
      pad: u8
      Size: u32
      Skip: u16
    """
    if len(payload) < 8:
        return None
    method = payload[0]
    pad = payload[1]
    size = u32le(payload, 2)
    skip = payload[6] | (payload[7] << 8)

    # Conservative sanity checks
    if method > 4:
        return None
    if skip > 512:
        return None
    return {"method": method, "size": size, "skip": skip}


def rle_uncompress(src: bytes, out_len: int) -> Optional[bytes]:
    # Mirrors ATARILIB/iff.cpp RLE_Uncompress:
    #   if byte > 192: (byte-192) copies of next byte
    #   else: literal byte
    s = 0
    out = bytearray()
    while len(out) < out_len and s < len(src):
        code = src[s]
        s += 1
        if code > 192:
            if s >= len(src):
                break
            run_length = code - 192
            value = src[s]
            s += 1
            take = min(run_length, out_len - len(out))
            out += bytes([value]) * take
        else:
            out.append(code)
    if len(out) != out_len:
        return None
    return bytes(out)


def decode_shape_payload(shape_ptr: bytes, header_info: dict) -> Tuple[Optional[bytes], dict]:
    """
    Decode a mouse cursor shape payload to `width*height` bytes, matching
    `WIN32LIB/SRCDEBUG/WWMOUSE.ASM` logic:

    - Non-compact vs compact:
        header_size = 10 (normal) or 26 (compact with 16-byte colortable)
    - MAKESHAPE_NOCOMP means the bytes after the header are already in the
      NOCOMP RLE format used by ASM (`0x00 <count>` for transparent runs,
      otherwise a literal pixel byte).
    - Otherwise:
        payload is LCW-compressed; LCW output is the same NOCOMP RLE stream
        and its length must be `DataLength` (from the header).
      Then decode that RLE stream into final WxH pixels.
    """
    MAKESHAPE_COMPACT = 0x0001
    MAKESHAPE_NOCOMP = 0x0002

    shape_type = header_info["shape_type"]
    header_size = header_info["header_size"]
    width = header_info["width"]
    height = header_info["height"]
    need = header_info["need"]
    data_length = header_info["data_length"]
    compact = header_info["compact"]

    debug = {
        "shape_type": shape_type,
        "header_size": header_size,
        "need": need,
        "data_length": data_length,
        "compact": compact,
        "nocomp": header_info["nocomp"],
    }

    def decode_nocomp_rle(stream: bytes, out_len: int) -> Optional[bytes]:
        # ASM logic:
        #   ch = *esi++ ; if ch!=0 => write ch; else => count=*esi++; write zeros count
        out = bytearray()
        i = 0
        while len(out) < out_len and i < len(stream):
            b = stream[i]
            i += 1
            if b == 0:
                if i >= len(stream):
                    return None
                cnt = stream[i]
                i += 1
                take = min(cnt, out_len - len(out))
                out += b"\x00" * take
                if take != cnt:
                    # Would overflow output buffer => inconsistent stream
                    return None
            else:
                out.append(b)
        if len(out) != out_len:
            return None
        return bytes(out)

    def decode_nocomp_compact_rle(stream: bytes, remap: bytes, out_len: int) -> Optional[bytes]:
        # ASM:
        #   ebx = remap table
        #   b = *esi++ ; if b!=0 => pix=remap[b] else pix is transparent-run zeros count
        if len(remap) < 16:
            return None
        out = bytearray()
        i = 0
        while len(out) < out_len and i < len(stream):
            b = stream[i]
            i += 1
            if b == 0:
                if i >= len(stream):
                    return None
                cnt = stream[i]
                i += 1
                take = min(cnt, out_len - len(out))
                out += b"\x00" * take
                if take != cnt:
                    return None
            else:
                if b >= 16:
                    return None
                out.append(remap[b])
        if len(out) != out_len:
            return None
        return bytes(out)

    # Determine the location of the bytes after the header.
    # For both compressed and NOCOMP shapes, the "payload bytes" start at `header_size`.
    if header_info["nocomp"]:
        # Already in NOCOMP RLE format.
        debug["path"] = "nocomp_direct"
        if len(shape_ptr) < header_size + data_length:
            debug["fail_reason"] = "nocomp_stream_short"
            debug["available"] = max(0, len(shape_ptr) - header_size)
            return None, debug
        stream = shape_ptr[header_size : header_size + data_length]
    else:
        # Compressed: decode LCW to NOCOMP RLE stream of length `DataLength`.
        debug["path"] = "compressed_lcw_then_rle"
        lcw_src = shape_ptr[header_size:]
        rle = lcw_uncompress(lcw_src, data_length)
        if rle is None:
            debug["fail_reason"] = "lcw_uncompress_failed"
            debug["lcw_src_len"] = len(lcw_src)
            return None, debug
        stream = rle

    # Decode the NOCOMP RLE stream into WxH pixels.
    if compact:
        debug["rle_decode"] = "compact"
        remap = shape_ptr[10:26]
        pixels = decode_nocomp_compact_rle(stream, remap, need)
    else:
        debug["rle_decode"] = "normal"
        pixels = decode_nocomp_rle(stream, need)

    if pixels is None:
        debug["fail_reason"] = "rle_decode_failed"
        debug["stream_len"] = len(stream)
        return None, debug

    return pixels, debug


def dump_grid(pixels: bytes, w: int, h: int, max_w: int = 8, max_h: int = 8) -> None:
    rows = min(h, max_h)
    cols = min(w, max_w)
    for ry in range(rows):
        row = " ".join(f"{pixels[ry*w + cx]:02x}" for cx in range(cols))
        print(f"r{ry}: {row}")


def print_decode_failure_diagnostics(shape_ptr: bytes, hinfo: dict) -> None:
    """Extra output when LCW / CompHeader paths fail (helps compare with C++ debug)."""
    hs = hinfo["header_size"]
    data_length = hinfo.get("data_length", None)
    compact = hinfo.get("compact", False)
    nocomp = hinfo.get("nocomp", False)

    payload = shape_ptr[hs:]
    print(
        f"payload_len={len(payload)} (after {hs}-byte header) "
        f"data_length={data_length} compact={compact} nocomp={nocomp}"
    )
    nprev = min(32, len(payload))
    if nprev:
        print("payload_first_bytes:", " ".join(f"{b:02x}" for b in payload[:nprev]))
    if compact:
        remap = shape_ptr[10:26]
        print("remap_table[16]:", " ".join(f"{b:02x}" for b in remap[:16]))
    if (not nocomp) and len(payload) >= 16:
        # Helpful quick check: see if the stream at `header_size` looks like LCW.
        print("lcw_src_first_16:", " ".join(f"{b:02x}" for b in payload[:16]))


def print_frame_offset_table(data: bytes, num_shapes: int) -> None:
    """
    List every frame: index table position, raw s32 offset (MAKESHPS convention),
    absolute file pointer to Shape_Type (same as Extract_Shape: 2 + off), validity.
    For valid pointers, show quick header fields and declared ShapeSize (packed +6).
    """
    n = len(data)
    offsets_base = 2
    need_index = offsets_base + num_shapes * 4
    if need_index > n:
        print(
            f"WARNING: index extends past file ({need_index} > {n}); "
            "offsets may be incomplete."
        )

    print()
    print(
        "Frame offset table (off = s32 LE at 2+idx*4; ptr = 2+off = first byte of shape; "
        "same as C++ Extract_Shape):"
    )
    print(
        f"{'idx':>4}  {'tbl@':>6}  {'off':>10}  {'ptr':>8}  {'stat':^7}  "
        f"{'type':>4}  {'WxH':>7}  {'hdr':>3}  {'need':>4}  {'decl_sz':>7}  {'avail':>6}"
    )
    print("-" * 92)

    invalid_count = 0
    for fi in range(num_shapes):
        tbl_at = offsets_base + fi * 4
        off = s32le(data, tbl_at)
        ptr = 2 + off
        ok = 0 <= ptr < n
        if not ok:
            invalid_count += 1
            stat = "INVALID"
            print(
                f"{fi:4d}  {tbl_at:6d}  {off:10d}  {ptr:8d}  {stat:^7}  "
                f"{'—':>4}  {'—':>7}  {'—':>3}  {'—':>4}  {'—':>7}  {'—':>6}"
            )
            continue

        rest = data[ptr:]
        avail = len(rest)
        if avail < 8:
            print(
                f"{fi:4d}  {tbl_at:6d}  {off:10d}  {ptr:8d}  {'short':^7}  "
                f"{'—':>4}  {'—':>7}  {'—':>3}  {'—':>4}  {'—':>7}  {avail:6d}"
            )
            continue

        hinfo = read_shape_type_header(rest)
        # Declared total shape size in memory (header+data), packed layout: u16 @ +6
        decl_sz = u16le(rest, 6)
        wh = f"{hinfo['width']}x{hinfo['height']}"
        print(
            f"{fi:4d}  {tbl_at:6d}  {off:10d}  {ptr:8d}  {'ok':^7}  "
            f"{hinfo['shape_type']:4d}  {wh:>7}  {hinfo['header_size']:3d}  "
            f"{hinfo['need']:4d}  {decl_sz:7d}  {avail:6d}"
        )

    print("-" * 92)
    print(f"valid_ptr_frames={num_shapes - invalid_count}  INVALID_ptr={invalid_count}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("path", type=Path, help="Path to dumped MOUSE.SHP (shape block bytes).")
    ap.add_argument("--frame", type=int, default=0, help="Shape index to decode/dump.")
    ap.add_argument("--max-frames", type=int, default=0, help="If >0, summarize first N shapes.")
    ap.add_argument(
        "--no-offset-table",
        action="store_true",
        help="Do not print the full per-frame offset / ptr / header summary table.",
    )
    ap.add_argument("--try-all-offsets", action="store_true", help="No-op (kept for compatibility).")
    args = ap.parse_args()

    data = args.path.read_bytes()
    if len(data) < 4:
        raise SystemExit("File too small to be a shape block.")

    num_shapes = u16le(data, 0)
    print(f"NumShapes={num_shapes} file_size={len(data)} bytes")

    # ShapeBlock_Type: [u16 num][s32 offsets...]
    offsets_base = 2
    max_frames = num_shapes if args.max_frames <= 0 else min(num_shapes, args.max_frames)

    def extract_shape_ptr(frame_index: int) -> Optional[bytes]:
        if frame_index < 0 or frame_index >= num_shapes:
            return None
        off = s32le(data, offsets_base + frame_index * 4)
        ptr = 2 + off
        if ptr < 0 or ptr >= len(data):
            return None
        return data[ptr:]

    if not args.no_offset_table:
        print_frame_offset_table(data, num_shapes)

    # Summary (legacy; overlaps with offset table but keeps --max-frames behavior)
    if max_frames > 0 and args.max_frames > 0:
        print()
        print(f"--max-frames={args.max_frames} detail:")
        for fi in range(max_frames):
            shape_ptr = extract_shape_ptr(fi)
            if not shape_ptr or len(shape_ptr) < 28:
                print(f"[{fi}] ptr invalid")
                continue
            # Parse packed header info
            hinfo = read_shape_type_header(shape_ptr)
            print(f"[{fi}] type={hinfo['shape_type']} w={hinfo['width']} h={hinfo['height']} hdr={hinfo['header_size']} need={hinfo['need']}")

    # Decode requested frame
    fi = args.frame
    shape_ptr = extract_shape_ptr(fi)
    if not shape_ptr:
        off = s32le(data, offsets_base + fi * 4)
        ptr = 2 + off
        raise SystemExit(
            f"Frame {fi} extraction failed (bad offset): off={off} ptr={ptr} file_size={len(data)}"
        )

    hinfo = read_shape_type_header(shape_ptr)
    off_fi = s32le(data, offsets_base + fi * 4)
    ptr_fi = 2 + off_fi
    print(f"\nFrame={fi}  (off={off_fi} ptr={ptr_fi})")
    print(f"shape_type={hinfo['shape_type']} height={hinfo['height']} width={hinfo['width']} hdr={hinfo['header_size']} need={hinfo['need']}")
    print("first_shape_hdr_bytes:", " ".join(f"{b:02x}" for b in shape_ptr[:16]))
    pix, dec_debug = decode_shape_payload(shape_ptr, hinfo)
    print("decode_debug:", dec_debug)
    if pix is None:
        print("Decoded pixels: None (failed).")
        print_decode_failure_diagnostics(shape_ptr, hinfo)
        return 0
    dump_grid(pix, hinfo["width"], hinfo["height"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

