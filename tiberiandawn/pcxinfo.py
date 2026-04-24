#!/usr/bin/env python3
import sys
import struct
from pathlib import Path

PCX_HEADER_SIZE = 128

def parse_pcx_header(data: bytes):
    """
    Parse a standard 128-byte PCX header (ZSoft .PCX).
    Returns a dict with the most relevant fields.
    """
    if len(data) < PCX_HEADER_SIZE:
        raise ValueError("File too small to be a valid PCX (header < 128 bytes).")

    # PCX header layout (little-endian)
    # Referencing classic PCX spec:
    #  0  BYTE  Manufacturer (should be 0x0A)
    #  1  BYTE  Version
    #  2  BYTE  Encoding
    #  3  BYTE  BitsPerPixel
    #  4  WORD  Xmin
    #  6  WORD  Ymin
    #  8  WORD  Xmax
    # 10  WORD  Ymax
    # 12  WORD  Hres
    # 14  WORD  Vres
    # 16  48B   16-color palette
    # 64  BYTE  Reserved
    # 65  BYTE  ColorPlanes
    # 66  WORD  BytesPerLine
    # 68  WORD  PaletteType
    # 70  58B   Filler
    header_struct = "<BBBBHHHHHH48sB B H H58s"
    fields = struct.unpack(header_struct, data[:PCX_HEADER_SIZE])

    (
        manufacturer,
        version,
        encoding,
        bits_per_pixel,
        xmin, ymin,
        xmax, ymax,
        hres, vres,
        ega_palette,
        reserved,
        color_planes,
        bytes_per_line,
        palette_type,
        filler,
    ) = fields

    width = xmax - xmin + 1
    height = ymax - ymin + 1

    return {
        "manufacturer": manufacturer,
        "version": version,
        "encoding": encoding,
        "bits_per_pixel": bits_per_pixel,
        "xmin": xmin,
        "ymin": ymin,
        "xmax": xmax,
        "ymax": ymax,
        "width": width,
        "height": height,
        "hres": hres,
        "vres": vres,
        "color_planes": color_planes,
        "bytes_per_line": bytes_per_line,
        "palette_type": palette_type,
        "reserved": reserved,
    }

def describe_version(v: int) -> str:
    return {
        0: "Version 2.5",
        2: "Version 2.8 with palette",
        3: "Version 2.8 without palette",
        4: "PC Paintbrush for Windows",
        5: "Version 3.0+",
    }.get(v, "Unknown")

def describe_encoding(e: int) -> str:
    return {
        0: "Uncompressed",
        1: "RLE compressed",
    }.get(e, "Unknown")

PALETTE_MARKER = 0x0C
PALETTE_SIZE = 256 * 3  # 768 bytes

def read_pcx_palette(f) -> bytes | None:
    """
    Read the 256-color palette from end of file (0x0C + 768 bytes).
    Returns 768 bytes (256 RGB triplets) or None if not present/invalid.
    """
    f.seek(0, 2)
    size = f.tell()
    if size < 1 + PALETTE_SIZE:
        return None
    f.seek(-(1 + PALETTE_SIZE), 2)
    marker = f.read(1)[0]
    if marker != PALETTE_MARKER:
        return None
    return f.read(PALETTE_SIZE)

def dump_palette(palette: bytes):
    """Print palette entries in hex (first 6, then 168-173, then summary)."""
    if len(palette) < PALETTE_SIZE:
        print("  (palette truncated)")
        return
    print("  Palette (256 colors, R G B hex):")
    for label, indices in [("0-5", range(6)), ("168-173", range(168, 174))]:
        line = f"    [{label}]:"
        for i in indices:
            off = i * 3
            r, g, b = palette[off], palette[off + 1], palette[off + 2]
            line += f" [{i}]=%02X %02X %02X" % (r, g, b)
        print(line)
    # Show first/last as sample
    r0, g0, b0 = palette[0], palette[1], palette[2]
    r255, g255, b255 = palette[765], palette[766], palette[767]
    print(f"    [0]=%02X %02X %02X  ...  [255]=%02X %02X %02X" % (r0, g0, b0, r255, g255, b255))

def main(argv):
    if len(argv) != 2:
        print(f"Usage: {Path(argv[0]).name} <file.pcx>")
        return 1

    path = Path(argv[1])
    if not path.is_file():
        print(f"Error: file not found: {path}")
        return 1

    with path.open("rb") as f:
        header = f.read(PCX_HEADER_SIZE)
        palette = read_pcx_palette(f)

    try:
        info = parse_pcx_header(header)
    except Exception as e:
        print(f"Error parsing PCX header: {e}")
        return 1

    print(f"File: {path}")
    print("PCX header:")
    print(f"  Manufacturer:   0x{info['manufacturer']:02X}")
    print(f"  Version:        {info['version']} ({describe_version(info['version'])})")
    print(f"  Encoding:       {info['encoding']} ({describe_encoding(info['encoding'])})")
    print(f"  BitsPerPixel:   {info['bits_per_pixel']}")
    print(f"  Xmin,Ymin:      {info['xmin']},{info['ymin']}")
    print(f"  Xmax,Ymax:      {info['xmax']},{info['ymax']}")
    print(f"  Width,Height:   {info['width']} x {info['height']}")
    print(f"  HRes,VRes:      {info['hres']} x {info['vres']}")
    print(f"  ColorPlanes:    {info['color_planes']}")
    print(f"  BytesPerLine:   {info['bytes_per_line']}")
    print(f"  PaletteType:    {info['palette_type']}")
    print(f"  Reserved:       {info['reserved']}")

    # Extra sanity checks useful for debugging:
    if info["manufacturer"] != 0x0A:
        print("  WARNING: Manufacturer byte is not 0x0A (may not be a PCX file).")
    if info["bytes_per_line"] % 2 != 0:
        print("  WARNING: BytesPerLine is not even (violates common PCX spec).")

    if info["bits_per_pixel"] == 8 and palette is not None:
        print("Palette (256-color):")
        dump_palette(palette)
    elif info["bits_per_pixel"] == 8:
        print("Palette: not found or invalid (no 0x0C + 768 bytes at end).")

    return 0

if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

