#!/usr/bin/env python3
"""Extract vanilla W2.TEM from TEMPERAT.MIX into st16_w2_tem_embed.h."""

from __future__ import annotations

import struct
import sys
from pathlib import Path

OUT = Path(__file__).with_name("st16_w2_tem_embed.h")
ENTRY = b"W2.TEM"


def mix_crc(name: str) -> int:
    data = name.upper().encode("ascii")
    crc = 0
    for i in range((len(data) + 3) >> 2):
        cs = i * 4
        avail = len(data) - cs
        value = (
            (data[cs] if avail > 0 else 0)
            | ((data[cs + 1] << 8) if avail > 1 else 0)
            | ((data[cs + 2] << 16) if avail > 2 else 0)
            | ((data[cs + 3] << 24) if avail > 3 else 0)
        )
        high_bit = 1 if (crc & 0x80000000) else 0
        crc = ((crc << 1) | high_bit) & 0xFFFFFFFF
        crc = (crc + value) & 0xFFFFFFFF
    return crc


def extract_w2(mix_path: Path) -> bytes:
    mix = mix_path.read_bytes()
    count = struct.unpack_from("<H", mix, 0)[0]
    base = 6 + count * 12
    want = mix_crc(ENTRY.decode())
    for i in range(count):
        off = 6 + i * 12
        crc, entry_off, size = struct.unpack_from("<III", mix, off)
        if crc == want:
            blob = mix[base + entry_off : base + entry_off + size]
            icons = struct.unpack_from("<I", blob, 12)[0]
            if icons != 0x20:
                raise SystemExit(
                    f"error: {ENTRY.decode()} is not PC-standard (Icons=0x{icons:X}); "
                    "use an unremixed TEMPERAT.MIX"
                )
            return blob
    raise SystemExit(f"error: {ENTRY.decode()} not found in {mix_path}")


def write_header(blob: bytes) -> None:
    lines = [
        "/* Embedded vanilla PC W2.TEM (coast iconset, map [0,1,2,3]) for ST16 convert regression.",
        " * Regenerate: tests/st_suite/extract_st16_w2_tem.py PATH/TO/TEMPERAT.MIX",
        " */",
        "#ifndef ST16_W2_TEM_EMBED_H_",
        "#define ST16_W2_TEM_EMBED_H_",
        "",
        "#include <stdint.h>",
        "",
        f"#define ST16_W2_TEM_EMBED_SIZE {len(blob)}u",
        "",
        f"static uint8_t const k_st16_w2_tem_embed[{len(blob)}] = {{",
    ]
    for i in range(0, len(blob), 16):
        chunk = blob[i : i + 16]
        lines.append("    " + ", ".join(f"0x{b:02x}u" for b in chunk) + ",")
    lines[-1] = lines[-1].rstrip(",")
    lines += ["};", "", "#endif /* ST16_W2_TEM_EMBED_H_ */", ""]
    OUT.write_text("\n".join(lines))


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {Path(sys.argv[0]).name} TEMPERAT.MIX")
    blob = extract_w2(Path(sys.argv[1]))
    write_header(blob)
    print(f"wrote {OUT} ({len(blob)} bytes)")


if __name__ == "__main__":
    main()
