#!/usr/bin/env python3
"""Rename .w16 sidecars between VQA basename and Westwood CRC forms.

to-crc:   NAME.<n>.w16  ->  xxxxxxxx.<n>.w16
  where xxxxxxxx is the lowercase 8-digit Westwood CRC of "NAME.VQA"

from-crc: xxxxxxxx.<n>.w16  ->  NAME.<n>.w16
  looking up NAME via the list_mix CRC database

By default files are moved into an output directory. Use -l to hardlink
or -s to symlink instead. Unknown CRCs and existing destinations are
warned about and skipped. Use --dry-run to preview.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import sys
from pathlib import Path

from list_mix import calculate_crc, read_mix_database

TO_CRC_RE = re.compile(r"^([^./]+)\.(\d+)\.w16$", re.IGNORECASE)
FROM_CRC_RE = re.compile(r"^([0-9a-fA-F]{8})\.(\d+)\.w16$", re.IGNORECASE)


def warn(msg: str) -> None:
    print(f"warning: {msg}", file=sys.stderr)


def vqa_crc(name: str) -> int:
    """Westwood CRC of uppercase 'NAME.VQA'."""
    return calculate_crc(f"{name.upper()}.VQA".encode("ascii"))


def place_file(src: Path, dest: Path, *, mode: str, dry_run: bool) -> bool:
    if dest.exists():
        warn(f"destination exists, skipping: {dest}")
        return False
    if dry_run:
        print(f"dry: {src.name} -> {dest}", flush=True)
        return True
    if mode == "hardlink":
        os.link(src, dest)
    elif mode == "symlink":
        os.symlink(src.resolve(), dest)
    else:
        shutil.move(str(src), str(dest))
    print(f"{src.name} -> {dest}", flush=True)
    return True


def cmd_to_crc(
    files: list[Path],
    outdir: Path,
    database: dict[int, tuple[str, str]],
    *,
    mode: str,
    dry_run: bool,
) -> int:
    ok = 0
    for src in files:
        m = TO_CRC_RE.match(src.name)
        if not m:
            warn(f"not NAME.<n>.w16, skipping: {src}")
            continue
        name, number = m.group(1), m.group(2)
        crc = vqa_crc(name)
        if crc not in database:
            warn(f"CRC 0x{crc:08X} for {name.upper()}.VQA not in database, skipping: {src}")
            continue
        db_name, _desc = database[crc]
        if db_name != f"{name.upper()}.VQA":
            warn(
                f"CRC 0x{crc:08X} maps to {db_name!r}, expected "
                f"{name.upper()}.VQA, skipping: {src}"
            )
            continue
        dest = outdir / f"{crc:08x}.{number}.w16"
        if place_file(src, dest, mode=mode, dry_run=dry_run):
            ok += 1
    return ok


def cmd_from_crc(
    files: list[Path],
    outdir: Path,
    database: dict[int, tuple[str, str]],
    *,
    mode: str,
    dry_run: bool,
) -> int:
    ok = 0
    for src in files:
        m = FROM_CRC_RE.match(src.name)
        if not m:
            warn(f"not xxxxxxxx.<n>.w16, skipping: {src}")
            continue
        crc_hex, number = m.group(1), m.group(2)
        crc = int(crc_hex, 16)
        if crc not in database:
            warn(f"CRC 0x{crc:08X} not in database, skipping: {src}")
            continue
        db_name, _desc = database[crc]
        if not db_name.endswith(".VQA"):
            warn(f"CRC 0x{crc:08X} is {db_name!r}, not a .VQA, skipping: {src}")
            continue
        stem = db_name[: -len(".VQA")]
        if "." in stem:
            warn(f"database name {db_name!r} has unexpected dots, skipping: {src}")
            continue
        dest = outdir / f"{stem}.{number}.w16"
        if place_file(src, dest, mode=mode, dry_run=dry_run):
            ok += 1
    return ok


def place_mode(args: argparse.Namespace) -> str:
    if args.hardlink:
        return "hardlink"
    if args.symlink:
        return "symlink"
    return "move"


def place_verb(mode: str, *, dry_run: bool) -> str:
    verbs = {
        "hardlink": ("would hardlink", "hardlinked"),
        "symlink": ("would symlink", "symlinked"),
        "move": ("would move", "moved"),
    }
    dry, done = verbs[mode]
    return dry if dry_run else done


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Place .w16 files between NAME.<n>.w16 and CRC.<n>.w16 forms"
    )
    parser.add_argument(
        "-d",
        "--database",
        metavar="DATABASE",
        help="Optional mix database file (default: list_mix embedded database)",
    )
    parser.add_argument(
        "-n",
        "--dry-run",
        action="store_true",
        help="Show planned actions without creating files or directories",
    )
    link = parser.add_mutually_exclusive_group()
    link.add_argument(
        "-l",
        "--hardlink",
        action="store_true",
        help="Hardlink into outdir instead of moving",
    )
    link.add_argument(
        "-s",
        "--symlink",
        action="store_true",
        help="Symlink into outdir instead of moving",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    def add_common(p: argparse.ArgumentParser) -> None:
        p.add_argument(
            "-o",
            "--outdir",
            required=True,
            type=Path,
            help="Directory to place renamed files into",
        )
        p.add_argument(
            "files",
            nargs="+",
            type=Path,
            help="Input .w16 files",
        )

    p_to = sub.add_parser("to-crc", help="NAME.<n>.w16 -> xxxxxxxx.<n>.w16")
    add_common(p_to)
    p_from = sub.add_parser("from-crc", help="xxxxxxxx.<n>.w16 -> NAME.<n>.w16")
    add_common(p_from)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    mode = place_mode(args)

    if args.database:
        database = read_mix_database(args.database)
    else:
        database = read_mix_database()

    outdir: Path = args.outdir
    if not args.dry_run:
        outdir.mkdir(parents=True, exist_ok=True)

    missing = [p for p in args.files if not p.is_file()]
    for p in missing:
        warn(f"not a file, skipping: {p}")
    files = [p for p in args.files if p.is_file()]
    if not files:
        warn("no input files to process")
        return 1

    if args.command == "to-crc":
        ok = cmd_to_crc(files, outdir, database, mode=mode, dry_run=args.dry_run)
    else:
        ok = cmd_from_crc(files, outdir, database, mode=mode, dry_run=args.dry_run)

    failed = len(files) - ok
    if failed:
        warn(f"{place_verb(mode, dry_run=args.dry_run)} {ok}, skipped {failed}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
