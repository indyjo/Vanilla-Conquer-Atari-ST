# remix

Repack C&C MIX archives for the Atari ST port.

For each embedded file the tool autodetects the asset type, converts audio to
11025 Hz 8-bit mono PCM `.AUD` where needed (IMA99, Westwood compression type 1,
PCM stereo/16-bit/other rates), optionally converts terrain iconsets in theater
MIX files to **ST16** planar format (requires matching `*.W16` C2P weights),
optionally convert KeyFrame SHPs to **SHPX** (`--shpx`),
and pad payloads to even byte offsets from the MIX start.

Plain TD-style MIX files only (no encrypted or extended headers).

## Build

Host utility:

```bash
cd tiberiandawn/tools/remix
make
```

Smoke test (ST16 conversion with `../../atari-assets/temperat.w16`):

```bash
make test-st16
```

SHPX / KeyFrame decode regression (curated shapes under `testdata/shapes/`):

```bash
make test-shpx
```

Regenerate test blobs from an unmodified `CONQUER.MIX`:

```bash
python3 testdata/extract_shapes.py /path/to/conquer.mix
```

MiNT `remix.tos` (cross-compiler):

```bash
make mint
```

The `itch-release` target in `tiberiandawn/makefile` builds `remix.tos` and
includes it in the release zip next to `cnc.tos`.

## Host usage

**Single file** (output path must differ from input):

```bash
./remix -o output.mix input.mix
```

Place `TEMPERAT.W16`, `DESERT.W16`, etc. in the current directory (or pass
`--w16-dir`) when repacking theater MIX files. ST16 conversion is **on by default**.

**Directory** (non-recursive; `.mix` / `.MIX`):

```bash
./remix -d /path/to/gamedata
```

## MiNT usage (`remix.tos`)

No arguments. Place `remix.tos` in the game folder with the `.mix` files and
matching `*.W16` files, then run once before `cnc.tos`. The tool writes
`temp.mxx` while working on each archive, then replaces the source `.mix` in place.

## Options (host only)

| Option | Meaning |
|--------|---------|
| `-o`, `--output PATH` | Output MIX file, or output directory with `-d` |
| `-d`, `--directory DIR` | Process all MIX files in `DIR` |
| `--w16-dir PATH` | Directory containing `TEMPERAT.W16` etc. (default: cwd) |
| `--no-st16-iconsets` | Skip ST16 iconset conversion in theater MIX files |
| `--shpx` | Convert KeyFrame SHPs to SHPX + `poolnnnn.bin` sidecar |
| `--shpx-verbose` | Per-shape SHPX/clip log on stderr (requires `--shpx`) |
| `--pool-id ID` | SHPX pool id (default `1`; requires `--shpx`) |
| `-h`, `--help` | Show help |

See [spec.md](spec.md) for ST16 scope, detection types (`icn` / `st16`), and failure policy.
