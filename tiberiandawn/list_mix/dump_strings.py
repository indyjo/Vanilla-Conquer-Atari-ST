#!/usr/bin/env python3
"""Dump C&C strings blobs (e.g. CONQUER.ENG) in a readable/parseable format."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Dict, List


def load_symbol_map(path: Path) -> Dict[int, List[dict]]:
    by_id: Dict[int, List[dict]] = {}
    with path.open("r", encoding="utf-8") as f:
        for line_no, line in enumerate(f, start=1):
            line = line.strip()
            if not line:
                continue
            row = json.loads(line)
            if "id" not in row:
                raise ValueError(f"{path}:{line_no}: missing 'id' field")
            key = int(row["id"])
            by_id.setdefault(key, []).append(row)
    return by_id


def decode_text(data: bytes, encoding: str) -> str:
    if encoding == "auto":
        for candidate in ("cp1252", "latin-1", "utf-8"):
            try:
                return data.decode(candidate)
            except UnicodeDecodeError:
                continue
        return data.decode("latin-1", errors="replace")
    return data.decode(encoding, errors="replace")


def infer_count(offsets: List[int], file_len: int) -> int:
    if not offsets:
        raise ValueError("empty file")

    first = offsets[0]
    if first % 2 != 0 or first < 2:
        raise ValueError(f"invalid first offset {first}, expected even >= 2")
    count = first // 2
    if count > len(offsets):
        raise ValueError(f"offset table claims {count} entries, file only has {len(offsets)}")

    if offsets[count - 1] >= file_len:
        raise ValueError("offset table points past end of file")

    for i in range(count - 1):
        if offsets[i] > offsets[i + 1]:
            raise ValueError(f"offsets not monotonic at index {i}: {offsets[i]} > {offsets[i + 1]}")
        if offsets[i] >= file_len:
            raise ValueError(f"offset at index {i} is out of file bounds: {offsets[i]}")

    return count


def parse_strings_blob(blob: bytes, encoding: str) -> List[str]:
    if len(blob) < 2:
        raise ValueError("file too small")
    offsets = [int.from_bytes(blob[i : i + 2], "little") for i in range(0, len(blob) - 1, 2)]
    count = infer_count(offsets, len(blob))
    strings: List[str] = []

    for idx in range(count):
        start = offsets[idx]
        if idx + 1 < count:
            end = offsets[idx + 1]
            raw = blob[start:end]
            raw = raw.split(b"\x00", 1)[0]
        else:
            end = blob.find(b"\x00", start)
            raw = blob[start:] if end == -1 else blob[start:end]
        strings.append(decode_text(raw, encoding))
    return strings


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Dump a C&C language strings blob (e.g. CONQUER.ENG)."
    )
    parser.add_argument("strings_file", help="Path to extracted strings blob file.")
    parser.add_argument(
        "--symbols",
        default="conquer_text_ids.jsonl",
        help="Path to JSONL symbol map generated from conquer.h.",
    )
    parser.add_argument(
        "--format",
        choices=("jsonl", "tsv"),
        default="jsonl",
        help="Output format.",
    )
    parser.add_argument(
        "--encoding",
        default="auto",
        help="Text decoding to use (default: auto -> cp1252/latin-1/utf-8 fallback).",
    )
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    symbols_path = Path(args.symbols)
    if not symbols_path.is_absolute():
        symbols_path = (script_dir / symbols_path).resolve()

    blob = Path(args.strings_file).read_bytes()
    strings = parse_strings_blob(blob, args.encoding)
    symbol_map = load_symbol_map(symbols_path)

    for idx, text in enumerate(strings):
        rows = symbol_map.get(idx, [])
        if rows:
            symbol = str(rows[0].get("symbol", ""))
            comment = str(rows[0].get("comment", ""))
        else:
            symbol = ""
            comment = ""

        if args.format == "tsv":
            # Escape tabs/newlines so TSV remains parseable.
            escaped = (
                text.replace("\\", "\\\\")
                .replace("\t", "\\t")
                .replace("\r", "\\r")
                .replace("\n", "\\n")
            )
            print(f"{idx}\t{symbol}\t{comment}\t{escaped}")
        else:
            out = {
                "id": idx,
                "symbol": symbol,
                "comment": comment,
                "text": text,
            }
            print(json.dumps(out, ensure_ascii=False))


if __name__ == "__main__":
    main()
