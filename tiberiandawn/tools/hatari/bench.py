#!/usr/bin/env python3
"""Run a C&C playback benchmark under Hatari.

Pass only --candidate to autostart that binary and print the timing block.
Pass --baseline as well to run both and compare ST-low screen dumps.

Each run gets a private GEMDOS directory (symlinks to the game disk, its own
CONQUER.INI, no MOVIES.MIX). Hatari autostarts the program as cnc.tos so the
desktop does not ask for a command line; the debugger writes the arguments
into the basepage. Hatari is stopped when the fps line is printed.

With a baseline, every Main_Loop entry during the timed run dumps the 32000
bytes of ST RAM at the shifter's screen base. Hatari's screenshot command is
not used: it saves what the beam has scanned, so a present that the beam has
not reached yet would show up a frame late or torn. The dumps are named from
the emulated VBL and the cycles since that VBL, then paired in that order. Those numbers differ between a fast and
a slow binary; only the sequence index is compared. Pixels in the on-screen
frame meter (x 80..159, y 0..7) are ignored; that readout changes with speed.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import dataclass
from pathlib import Path

DEFAULT_HATARI = (
    "/Users/jonas/Documents/devel/gcc/hatari/build/src/Hatari.app/Contents/MacOS/Hatari"
)
DEFAULT_HD = "/Users/jonas/Documents/Emu/Atari/HD/cnc"
DEFAULT_CONFIG = Path.home() / "Library/Application Support/Hatari/hatari.cfg"
DEFAULT_ARGS = "-xyq"
DEFAULT_MAX_DIFF = 64
DEFAULT_TIMEOUT = 3 * 60 * 60

ST_LOW_W = 320
ST_LOW_H = 200
LINE_BYTES = 160  # 4 bitplanes, 320 pixels
SCREEN_BYTES = ST_LOW_H * LINE_BYTES
# STe video base: high, mid, and low byte registers.
SCREEN_BASE_EXPR = "($ff8201).b*65536+($ff8203).b*256+($ff820d).b"

SKIP_NAMES = {
    "movies.mix",
    "conquer.ini",
    "record.bin",
    "cnc.tos",
    "cnc.ttp",
    "cnc.sym",
}

SNAP_RE = re.compile(r"^f\$([0-9a-fA-F]+)-\$([0-9a-fA-F]+)\.scr$")
FRAMES_RE = re.compile(r"frames:\s*(\d+)")
TICKS_RE = re.compile(r"ticks:\s*(\d+)")
TIME_RE = re.compile(r"time:\s*(\d+:\d{2}\.\d{2})")
FPS_RE = re.compile(r"fps:\s*([0-9]+(?:\.[0-9]+)?)")
SYM_RE = re.compile(r"^([0-9a-fA-F]+)\s+([A-Za-z])\s+(\S+)\s*$")


@dataclass
class Timing:
    frames: int | None = None
    ticks: int | None = None
    time: str | None = None
    fps: str | None = None

    def complete(self) -> bool:
        return None not in (self.frames, self.ticks, self.time, self.fps)

    def format(self) -> str:
        if not self.complete():
            return "incomplete"
        return (
            f"frames: {self.frames}  ticks: {self.ticks}  "
            f"time: {self.time}  fps: {self.fps}"
        )


@dataclass
class PixelReport:
    baseline_frames: int
    candidate_frames: int
    compared: int
    exact: int
    worst_index: int
    worst_pixels: int
    mean_pixels: float
    diff_ppm: Path | None

    def passed(self, max_diff_pixels: int) -> bool:
        return (
            self.baseline_frames > 0
            and self.baseline_frames == self.candidate_frames
            and self.worst_pixels <= max_diff_pixels
        )


def force_play_intro_no(text: str) -> str:
    """Set [Intro] PlayIntro=No, keeping the rest of the file."""
    newline = "\r\n" if "\r\n" in text else "\n"
    normalized = text.replace("\r\n", "\n").replace("\r", "\n")
    lines = normalized.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    header_at = None
    for i, line in enumerate(lines):
        if line.strip().lower() == "[intro]":
            header_at = i
            break
    if header_at is None:
        if lines:
            lines.append("")
        lines.extend(["[Intro]", "PlayIntro=No"])
    else:
        end = len(lines)
        for i in range(header_at + 1, len(lines)):
            if re.match(r"^\s*\[.+\]\s*$", lines[i]):
                end = i
                break
        replaced = False
        for i in range(header_at + 1, end):
            if re.match(r"(?i)^\s*PlayIntro\s*=", lines[i]):
                lines[i] = "PlayIntro=No"
                replaced = True
                break
        if not replaced:
            lines.insert(header_at + 1, "PlayIntro=No")
    return newline.join(lines) + newline


def debugger_token(name: str) -> str:
    """Portion of a symbol Hatari's expression parser accepts (no '.')."""
    token = []
    for ch in name:
        if ch.isalnum() or ch in "_:":
            token.append(ch)
        else:
            break
    return "".join(token)


def lookup_symbols(sym_text: str) -> dict[str, str]:
    """Map start/end/loop to debugger symbol tokens."""
    text_syms: list[str] = []
    for line in sym_text.splitlines():
        match = SYM_RE.match(line.strip())
        if not match:
            continue
        kind, name = match.group(2), match.group(3)
        if kind not in ("T", "t"):
            continue
        text_syms.append(name)

    def pick(predicate, label: str) -> str:
        found = [name for name in text_syms if predicate(name)]
        if not found:
            raise SystemExit(f"symbol file has no text symbol for {label}")
        found.sort(key=lambda name: (len(name), name))
        token = debugger_token(found[0])
        if not token:
            raise SystemExit(f"unusable symbol name for {label}: {found[0]}")
        # Hatari matches an exact symbol, or a single name that continues past '.' .
        exact = [name for name in found if name == token]
        partial = [name for name in found if debugger_token(name) == token]
        if not exact and len(partial) > 1:
            raise SystemExit(f"ambiguous {label} symbols: {', '.join(partial)}")
        return token

    return {
        "start": pick(lambda n: n == "HatariProfileStart" or n.startswith("HatariProfileStart"), "HatariProfileStart"),
        "end": pick(lambda n: n == "HatariProfileEnd" or n.startswith("HatariProfileEnd"), "HatariProfileEnd"),
        "loop": pick(
            lambda n: "Main_Loop" in n and "ST_Log" not in n and "On_Next" not in n,
            "Main_Loop",
        ),
    }


def find_entry(directory: Path, name: str) -> Path | None:
    folded = name.casefold()
    for entry in directory.iterdir():
        if entry.name.casefold() == folded:
            return entry
    return None


def stage_disk(hd: Path, dest: Path, program: Path, sym: Path | None, record_name: str | None) -> None:
    dest.mkdir(parents=True)
    for entry in hd.iterdir():
        if entry.name.casefold() in SKIP_NAMES:
            continue
        os.symlink(entry if entry.is_absolute() else entry.absolute(), dest / entry.name)

    if record_name:
        record_src = find_entry(hd, record_name)
        if record_src is None:
            raise SystemExit(f"recording {record_name} not found in {hd}")
    else:
        record_src = find_entry(hd, "record.bin")
        if record_src is None:
            raise SystemExit(f"record.bin not found in {hd} (pass --record)")
    os.symlink(record_src.resolve(), dest / "record.bin")

    ini_src = find_entry(hd, "conquer.ini")
    ini_text = ini_src.read_text(encoding="latin-1") if ini_src else ""
    (dest / "conquer.ini").write_bytes(force_play_intro_no(ini_text).encode("latin-1"))

    os.symlink(program.resolve(), dest / "cnc.tos")
    if sym is not None:
        os.symlink(sym.resolve(), dest / "cnc.sym")


def write_debugger_scripts(
    script_dir: Path,
    shot_dir: Path,
    args: str,
    symbols: dict[str, str] | None,
) -> Path:
    if len(args) > 126:
        raise SystemExit(f"command line is {len(args)} bytes; TOS basepage limit is 126")
    for path in (script_dir, shot_dir):
        if " " in str(path):
            raise SystemExit(f"path must not contain spaces: {path}")

    args_file = script_dir / "args.bin"
    args_file.write_bytes(args.encode("latin-1") + b"\0")

    snap = script_dir / "snap.ini"
    arm = script_dir / "arm.ini"
    disarm = script_dir / "disarm.ini"
    basepage = script_dir / "basepage.ini"
    text = script_dir / "text.ini"
    pexec = script_dir / "pexec.ini"

    # Quote expressions so Hatari substitutes $hex before running the command.
    basepage_lines = [
        "setopt dec",
        f"w 'basepage+0x80' {len(args)}",
        f"l {args_file} 'basepage+0x81'",
    ]
    if symbols is not None:
        snap.write_text(
            f"savebin {shot_dir}/f'VBL'-'FrameCycles'.scr '{SCREEN_BASE_EXPR}' {SCREEN_BYTES}\n"
        )
        arm.write_text(
            "b pc = {loop} :quiet :trace :noinit :file {snap}\n"
            "b pc = {end} :once :quiet :trace :noinit :file {disarm}\n".format(
                loop=symbols["loop"], end=symbols["end"], snap=snap, disarm=disarm
            )
        )
        disarm.write_text("b all\n")
        basepage_lines.extend(
            [
                "symbols prg",
                "b pc = {start} :once :quiet :trace :noinit :file {arm}".format(
                    start=symbols["start"], arm=arm
                ),
            ]
        )
    basepage.write_text("\n".join(basepage_lines) + "\n")
    text.write_text(f"b pc = TEXT :once :quiet :trace :noinit :file {basepage}\n")
    pexec.write_text(
        "b GemdosOpcode = 0x4B && OsCallParam = 0x0 :once :quiet :trace :noinit "
        f":file {text}\n"
    )
    return pexec


def decode_screen(data: bytes) -> tuple[int, int, list[int]]:
    """Return width, height, and one palette index per pixel of an ST-low dump."""
    if len(data) != SCREEN_BYTES:
        raise ValueError(f"screen dump is {len(data)} bytes, expected {SCREEN_BYTES}")
    width, height = ST_LOW_W, ST_LOW_H
    indices = [0] * (width * height)
    for y in range(height):
        row = data[y * LINE_BYTES : (y + 1) * LINE_BYTES]
        for group in range(width // 16):
            chunk = row[group * 8 : group * 8 + 8]
            planes = struct.unpack(">4H", chunk)
            for bit in range(16):
                shift = 15 - bit
                color = 0
                for plane in range(4):
                    if planes[plane] & (1 << shift):
                        color |= 1 << plane
                indices[y * width + group * 16 + bit] = color
    return width, height, indices


def encode_screen(indices: list[int], width: int = ST_LOW_W, height: int = ST_LOW_H) -> bytes:
    """Build an ST-low screen dump. Used by the self-test."""
    if len(indices) != width * height:
        raise ValueError("index count does not match dimensions")
    body = bytearray(height * LINE_BYTES)
    for y in range(height):
        for group in range(width // 16):
            words = [0, 0, 0, 0]
            for bit in range(16):
                color = indices[y * width + group * 16 + bit] & 0xF
                shift = 15 - bit
                for plane in range(4):
                    if color & (1 << plane):
                        words[plane] |= 1 << shift
            struct.pack_into(">4H", body, y * LINE_BYTES + group * 8, *words)
    return bytes(body)


# StFrameMeter_Draw on a 320-wide page: bar at x=80, width 80, rows 0..3,
# digits in that bar with an 8-pixel glyph height. The numbers track speed.
FPS_METER_X0 = 80
FPS_METER_Y0 = 0
FPS_METER_X1 = 160
FPS_METER_Y1 = 8


def in_fps_meter(index: int, width: int) -> bool:
    x = index % width
    y = index // width
    return FPS_METER_X0 <= x < FPS_METER_X1 and FPS_METER_Y0 <= y < FPS_METER_Y1


def count_diff(left: list[int], right: list[int], width: int = ST_LOW_W) -> int:
    if len(left) != len(right):
        raise ValueError("frame sizes differ")
    return sum(
        a != b and not in_fps_meter(i, width) for i, (a, b) in enumerate(zip(left, right))
    )


def write_diff_ppm(path: Path, width: int, height: int, left: list[int], right: list[int]) -> None:
    """Black where the palette index matches, red where it does not."""
    pixels = bytearray(width * height * 3)
    for i, (a, b) in enumerate(zip(left, right)):
        if a != b and not in_fps_meter(i, width):
            pixels[i * 3] = 255
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode("ascii") + pixels)


def snap_sort_key(path: Path) -> tuple[int, int]:
    match = SNAP_RE.match(path.name)
    if not match:
        raise ValueError(f"unexpected screen dump name: {path.name}")
    return (int(match.group(1), 16), int(match.group(2), 16))


def list_snaps(directory: Path) -> list[Path]:
    snaps = [path for path in directory.iterdir() if SNAP_RE.match(path.name)]
    snaps.sort(key=snap_sort_key)
    return snaps


def compare_snaps(baseline_dir: Path, candidate_dir: Path, diff_ppm: Path) -> PixelReport:
    baseline = list_snaps(baseline_dir)
    candidate = list_snaps(candidate_dir)
    compared = min(len(baseline), len(candidate))
    exact = 0
    total = 0
    worst_index = -1
    worst_pixels = 0
    worst_pair: tuple[list[int], list[int]] | None = None
    width = height = 0
    for index in range(compared):
        bw, bh, left = decode_screen(baseline[index].read_bytes())
        cw, ch, right = decode_screen(candidate[index].read_bytes())
        if (bw, bh) != (cw, ch):
            raise SystemExit(f"frame {index + 1} dimensions differ")
        width, height = bw, bh
        diff = count_diff(left, right, bw)
        total += diff
        if diff == 0:
            exact += 1
        if diff > worst_pixels:
            worst_pixels = diff
            worst_index = index
            worst_pair = (left, right)
    ppm_path = None
    if worst_pair is not None and worst_pixels > 0:
        write_diff_ppm(diff_ppm, width, height, worst_pair[0], worst_pair[1])
        ppm_path = diff_ppm
    mean = (total / compared) if compared else 0.0
    return PixelReport(
        baseline_frames=len(baseline),
        candidate_frames=len(candidate),
        compared=compared,
        exact=exact,
        worst_index=worst_index,
        worst_pixels=worst_pixels,
        mean_pixels=mean,
        diff_ppm=ppm_path,
    )


def note_timing(timing: Timing, line: str) -> None:
    if timing.frames is None and (match := FRAMES_RE.search(line)):
        timing.frames = int(match.group(1))
    if timing.ticks is None and (match := TICKS_RE.search(line)):
        timing.ticks = int(match.group(1))
    if timing.time is None and (match := TIME_RE.search(line)):
        timing.time = match.group(1)
    if timing.fps is None and (match := FPS_RE.search(line)):
        timing.fps = match.group(1)


def stop_process(proc: subprocess.Popen[str]) -> None:
    if proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)


def run_hatari(
    name: str,
    hatari: Path,
    config: Path | None,
    extra_args: list[str],
    program: Path,
    parse_file: Path,
    log_path: Path,
    timeout: float,
) -> Timing:
    cmd = [str(hatari)]
    if config is not None:
        cmd.extend(["--configfile", str(config)])
    cmd.extend(
        [
            "--fast-forward",
            "on",
            "--conout",
            "2",
            "--confirm-quit",
            "off",
        ]
    )
    cmd.extend(extra_args)
    cmd.extend(["--parse", str(parse_file), str(program)])
    print(f"{name}: {' '.join(cmd)}", flush=True)

    timing = Timing()
    done = threading.Event()
    with log_path.open("w", encoding="utf-8", errors="replace") as log:
        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            errors="replace",
            bufsize=1,
        )

        def read_output() -> None:
            assert proc.stdout is not None
            for line in proc.stdout:
                log.write(line)
                log.flush()
                note_timing(timing, line)
                if timing.complete() and not done.is_set():
                    done.set()
                    stop_process(proc)
            done.set()

        reader = threading.Thread(target=read_output, name=f"{name}-log", daemon=True)
        reader.start()
        deadline = time.monotonic() + timeout
        while not done.wait(timeout=0.5):
            if time.monotonic() >= deadline:
                print(f"{name}: timed out after {timeout:.0f}s", file=sys.stderr, flush=True)
                stop_process(proc)
                break
        reader.join(timeout=10)
        stop_process(proc)
    return timing


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help="check the frame compare without Hatari")
    parser.add_argument("--hd", type=Path, default=Path(DEFAULT_HD), help="game disk to clone")
    parser.add_argument("--hatari", type=Path, default=Path(DEFAULT_HATARI))
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG, help="Hatari config to copy per instance")
    parser.add_argument("--baseline", type=Path, help="known-good cnc.ttp or cnc.tos; omit to skip the pixel compare")
    parser.add_argument("--baseline-sym", type=Path, help="symbols for --baseline; required with --baseline")
    parser.add_argument("--candidate", type=Path, help="build to run")
    parser.add_argument("--candidate-sym", type=Path, help="symbols for --candidate; required with --baseline")
    parser.add_argument("--record", help="recording filename inside --hd (default: record.bin)")
    parser.add_argument("--args", default=DEFAULT_ARGS, help="basepage command line (default: -xyq)")
    parser.add_argument("--max-diff-pixels", type=int, default=DEFAULT_MAX_DIFF)
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT, help="seconds per instance")
    parser.add_argument("--output", type=Path, help="directory for logs, frames, and the diff image")
    parser.add_argument("hatari_args", nargs=argparse.REMAINDER, help="extra Hatari options, after --")
    args = parser.parse_args(argv)
    if args.hatari_args[:1] == ["--"]:
        args.hatari_args = args.hatari_args[1:]
    return args


def require_file(path: Path, label: str) -> None:
    if not path.is_file():
        raise SystemExit(f"{label} not found: {path}")


def programs_to_run(args: argparse.Namespace) -> list[tuple[str, Path, Path | None]]:
    """Return (name, program, sym or None). A missing baseline is a timing-only run."""
    if args.candidate is None:
        raise SystemExit("--candidate is required")
    if (args.baseline is None) != (args.baseline_sym is None):
        raise SystemExit("--baseline and --baseline-sym are both required to compare")
    if args.baseline is not None and args.candidate_sym is None:
        raise SystemExit("--candidate-sym is required when comparing against --baseline")
    if args.baseline is None and args.candidate_sym is not None:
        print("ignoring --candidate-sym; it is only used for the pixel compare", flush=True)
    runs = [("candidate", args.candidate, args.candidate_sym if args.baseline is not None else None)]
    if args.baseline is not None:
        runs.insert(0, ("baseline", args.baseline, args.baseline_sym))
    return runs


def run_pair(args: argparse.Namespace) -> int:
    runs = programs_to_run(args)
    for label, path in ((f"--{name}", program) for name, program, _sym in runs):
        require_file(path, label)
    for name, _program, sym in runs:
        if sym is not None:
            require_file(sym, f"--{name}-sym")
    require_file(args.hatari, "--hatari")
    if not args.hd.is_dir():
        raise SystemExit(f"--hd is not a directory: {args.hd}")
    compare = len(runs) > 1

    if args.output is None:
        output = Path(tempfile.mkdtemp(prefix="cnc-bench-"))
    else:
        output = args.output
        output.mkdir(parents=True, exist_ok=True)
    print(f"output: {output}", flush=True)

    config_src = args.config if args.config.is_file() else None
    if config_src is None:
        print(f"no Hatari config at {args.config}; using Hatari defaults", flush=True)

    sides = {}
    for name, program, sym in runs:
        side = output / name
        disk = side / "disk"
        shots = side / "shots"
        scripts = side / "dbg"
        shots.mkdir(parents=True)
        scripts.mkdir(parents=True)
        symbols = None
        if sym is not None:
            symbols = lookup_symbols(sym.read_text(encoding="latin-1", errors="replace"))
        parse_file = write_debugger_scripts(scripts, shots, args.args, symbols)
        stage_disk(args.hd, disk, program, sym, args.record)
        config_copy = None
        if config_src is not None:
            config_copy = side / "hatari.cfg"
            shutil.copyfile(config_src, config_copy)
        sides[name] = {
            "program": disk / "cnc.tos",
            "parse": parse_file,
            "shots": shots,
            "log": side / "hatari.log",
            "config": config_copy,
        }

    timings: dict[str, Timing] = {}
    errors: list[str] = []

    def launch(name: str) -> None:
        side = sides[name]
        try:
            timings[name] = run_hatari(
                name,
                args.hatari,
                side["config"],
                args.hatari_args,
                side["program"],
                side["parse"],
                side["log"],
                args.timeout,
            )
        except OSError as exc:
            print(f"{name}: {exc}", file=sys.stderr, flush=True)
            timings[name] = Timing()

    threads = [threading.Thread(target=launch, name=name, args=(name,)) for name, _program, _sym in runs]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()

    for name, _program, _sym in runs:
        timing = timings.get(name, Timing())
        print(f"{name}: {timing.format()}", flush=True)
        if not timing.complete():
            errors.append(f"{name} did not print a timing block (see {sides[name]['log']})")

    if not compare:
        if errors:
            for error in errors:
                print(error, file=sys.stderr, flush=True)
            return 1
        return 0

    report = compare_snaps(sides["baseline"]["shots"], sides["candidate"]["shots"], output / "worst.ppm")
    differing = report.compared - report.exact
    print(
        f"frames: baseline {report.baseline_frames}, candidate {report.candidate_frames}, "
        f"compared {report.compared}",
        flush=True,
    )
    print(
        f"pixels: exact {report.exact}, differing {differing}, "
        f"worst {report.worst_pixels}, mean {report.mean_pixels:.2f}",
        flush=True,
    )
    if report.worst_index >= 0 and report.worst_pixels > 0:
        print(f"worst frame: {report.worst_index + 1}", flush=True)
    if report.diff_ppm is not None:
        print(f"diff image: {report.diff_ppm}", flush=True)
    if report.baseline_frames != report.candidate_frames:
        errors.append("frame counts differ")
    elif report.baseline_frames == 0:
        errors.append("no frames captured")
    elif report.worst_pixels > args.max_diff_pixels:
        errors.append(
            f"worst frame differs by {report.worst_pixels} pixels "
            f"(limit {args.max_diff_pixels})"
        )
    if errors:
        for error in errors:
            print(error, file=sys.stderr, flush=True)
        return 1
    return 0


def run_self_test() -> int:
    blank = [0] * (ST_LOW_W * ST_LOW_H)
    painted = blank.copy()
    painted[0] = 1
    painted[1] = 2
    painted[16] = 15
    painted[ST_LOW_W * 10 + 20] = 7
    encoded = encode_screen(painted)
    width, height, decoded = decode_screen(encoded)
    assert (width, height) == (ST_LOW_W, ST_LOW_H)
    assert decoded == painted
    assert count_diff(painted, painted) == 0
    assert count_diff(painted, blank) == 4

    other = painted.copy()
    for i in range(10):
        other[ST_LOW_W * 20 + 100 + i] = 3
    assert count_diff(painted, other) == 10
    meter_only = painted.copy()
    meter_only[FPS_METER_Y0 * ST_LOW_W + FPS_METER_X0 + 4] = 9
    assert count_diff(painted, meter_only) == 0

    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        base = root / "base"
        cand = root / "cand"
        base.mkdir()
        cand.mkdir()
        (base / "f$a-$1.scr").write_bytes(encode_screen(painted))
        (base / "f$10-$1.scr").write_bytes(encode_screen(blank))
        (cand / "f$10-$1.scr").write_bytes(encode_screen(blank))
        (cand / "f$a-$1.scr").write_bytes(encode_screen(other))
        # Hex sort, not lexicographic: $a (10) is before $10 (16).
        assert [path.name for path in list_snaps(base)] == ["f$a-$1.scr", "f$10-$1.scr"]
        report = compare_snaps(base, cand, root / "worst.ppm")
        assert report.baseline_frames == report.candidate_frames == 2
        assert report.exact == 1
        assert report.worst_pixels == 10
        assert report.worst_index == 0
        assert report.passed(64)
        assert not report.passed(9)
        assert not report.passed(0)
        assert report.diff_ppm is not None and report.diff_ppm.stat().st_size > 0
        (cand / "extra.scr").write_bytes(b"nope")
        (cand / "f$30-$1.scr").write_bytes(encode_screen(blank))
        mismatch = compare_snaps(base, cand, root / "worst2.ppm")
        assert mismatch.baseline_frames != mismatch.candidate_frames
        assert not mismatch.passed(64)

    ini = force_play_intro_no("[Intro]\nPlayIntro=Yes\n\n[Options]\nGameSpeed=0\n")
    assert "PlayIntro=No" in ini and "PlayIntro=Yes" not in ini
    assert "[Options]" in ini
    added = force_play_intro_no("[Options]\nGameSpeed=0\n")
    assert "[Intro]" in added and "PlayIntro=No" in added
    empty = force_play_intro_no("")
    assert empty.startswith("[Intro]")

    symbols = lookup_symbols(
        "0004390e T _Z9Main_Loopv\n"
        "000ee148 T HatariProfileStart\n"
        "000ee200 T HatariProfileEnd\n"
        "001462b0 B _ZL33ST_Log_Free_Ram_On_Next_Main_Loop\n"
    )
    assert symbols == {"start": "HatariProfileStart", "end": "HatariProfileEnd", "loop": "_Z9Main_Loopv"}
    constprop = lookup_symbols(
        "0004390e T _Z9Main_Loopv.constprop.0\n"
        "000ee148 T HatariProfileStart\n"
        "000ee200 T HatariProfileEnd.constprop.0\n"
    )
    assert constprop["loop"] == "_Z9Main_Loopv"
    assert constprop["end"] == "HatariProfileEnd"
    try:
        lookup_symbols(
            "0001 T _Z9Main_Loopv.constprop.0\n"
            "0002 T _Z9Main_Loopv.constprop.1\n"
            "0003 T HatariProfileStart\n"
            "0004 T HatariProfileEnd\n"
        )
    except SystemExit:
        pass
    else:
        raise AssertionError("ambiguous Main_Loop was accepted")

    solo = programs_to_run(
        argparse.Namespace(candidate=Path("cnc.ttp"), candidate_sym=None, baseline=None, baseline_sym=None)
    )
    assert solo == [("candidate", Path("cnc.ttp"), None)]
    try:
        programs_to_run(
            argparse.Namespace(
                candidate=Path("cnc.ttp"), candidate_sym=None, baseline=Path("old.ttp"), baseline_sym=None
            )
        )
    except SystemExit:
        pass
    else:
        raise AssertionError("baseline without a symbol file was accepted")
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        scripts = root / "dbg"
        shots = root / "shots"
        scripts.mkdir()
        shots.mkdir()
        write_debugger_scripts(scripts, shots, "-xyq", None)
        basepage = (scripts / "basepage.ini").read_text()
        assert "symbols prg" not in basepage
        assert "HatariProfileStart" not in basepage
        assert "w 'basepage+0x80' 4" in basepage

    timing = Timing()
    note_timing(timing, "frames: 2285\n")
    note_timing(timing, "- 'VBL' -> $1a\n")
    note_timing(timing, "ticks: 115315\n")
    note_timing(timing, "time: 9:36.57\n")
    assert not timing.complete()
    note_timing(timing, "fps: 3.9631\n")
    assert timing.complete()
    assert timing.fps == "3.9631"
    print("self-test ok")
    return 0


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    if args.self_test:
        return run_self_test()
    return run_pair(args)


if __name__ == "__main__":
    sys.exit(main())
