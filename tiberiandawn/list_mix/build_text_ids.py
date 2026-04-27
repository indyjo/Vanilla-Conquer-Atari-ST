#!/usr/bin/env python3
"""Build a parseable TXT_* symbol map from conquer.h."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

DEFINE_RE = re.compile(
    r"^\s*#define\s+(TXT_[A-Za-z0-9_]+)\s+([0-9+\-() \t]+?)(?:\s*//(.*))?\s*$"
)


def eval_int_expr(expr: str) -> int:
    """Evaluate a tiny integer expression containing only digits, +, -, and parentheses."""
    allowed = set("0123456789+-() \t")
    if any(ch not in allowed for ch in expr):
        raise ValueError(f"unsupported token in expression: {expr!r}")

    # `eval` is safe here because we strictly whitelist characters.
    value = eval(expr, {"__builtins__": {}}, {})  # noqa: S307
    if not isinstance(value, int):
        raise ValueError(f"expression did not evaluate to int: {expr!r}")
    return value


def parse_conquer_header(path: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for line_no, line in enumerate(path.read_text(encoding="latin-1").splitlines(), start=1):
        match = DEFINE_RE.match(line)
        if not match:
            continue

        symbol = match.group(1)
        expr = match.group(2).strip()
        comment = (match.group(3) or "").rstrip()
        value = eval_int_expr(expr)

        rows.append(
            {
                "id": value,
                "symbol": symbol,
                "expr": expr,
                "comment": comment,
                "source_line": line_no,
            }
        )
    return rows


def write_jsonl(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as f:
        for row in sorted(rows, key=lambda x: (int(x["id"]), str(x["symbol"]))):
            f.write(json.dumps(row, ensure_ascii=False) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Extract TXT_* ids and comments from conquer.h into JSONL."
    )
    parser.add_argument(
        "--header",
        default="../conquer.h",
        help="Path to conquer.h (default: ../conquer.h relative to this script).",
    )
    parser.add_argument(
        "--output",
        default="conquer_text_ids.jsonl",
        help="Output JSONL mapping file.",
    )
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    header_path = (script_dir / args.header).resolve()
    output_path = (script_dir / args.output).resolve()

    rows = parse_conquer_header(header_path)
    write_jsonl(output_path, rows)
    print(f"Wrote {len(rows)} mappings to {output_path}")


if __name__ == "__main__":
    main()
