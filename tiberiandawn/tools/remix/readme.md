# remix

Repack C&C MIX archives for the Atari ST port.

For each embedded file the tool autodetects the asset type, converts audio to
11025 Hz 8-bit mono PCM `.AUD` where needed (IMA99, Westwood compression type 1,
PCM stereo/16-bit/other rates), optionally converts terrain iconsets in theater
MIX files to **ST16** planar format (requires matching `*.W16` C2P weights),
optionally convert KeyFrame SHPs to **SHPX** (`--shpx`),
optionally convert VQA movies to **STVQ** (`--convert-vqa`; needs
`video/xxxxxxxx.N.w16` sidecars under `--w16-dir`),
and pad payloads to even byte offsets from the MIX start.

Plain TD-style MIX files only (no encrypted or extended headers).

Prefer [remix-web](../remix-web/) for end-user MIX prep (browser WASM). This host
CLI is for developers and batch scripts.

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

Run both host regression suites:

```bash
make test
```

Regenerate test blobs from an unmodified `CONQUER.MIX`:

```bash
python3 testdata/extract_shapes.py /path/to/conquer.mix
```

## Host usage

**Single file** (output path must differ from input):

```bash
./remix -o output.mix input.mix
```

Place `TEMPERAT.W16`, `DESERT.W16`, etc. in the current directory (or pass
`--w16-dir`) when repacking theater MIX files. ST16 conversion is **on by default**.

For movies:

```bash
./remix -o movies.out.mix --convert-vqa --w16-dir ../../atari-assets \
  --video-quality medium --video-effort normal movies.mix
```

W16 sidecars are looked up as `{w16-dir}/video/{crc:08x}.{seg}.w16` where `crc`
is the MIX entry CRC (lowercase hex). Missing sidecars or encode failure: that
entry is **omitted** from the output MIX (warning on stderr). Already-converted
`FORM STVQ` payloads are copied unchanged.

**Directory** (non-recursive; `.mix` / `.MIX`):

```bash
./remix -d /path/to/gamedata
```

## Options

| Option | Meaning |
|--------|---------|
| `-o`, `--output PATH` | Output MIX file, or output directory with `-d` |
| `-d`, `--directory DIR` | Process all MIX files in `DIR` |
| `--w16-dir PATH` | Directory containing theater `*.W16` and `video/` (default: cwd) |
| `--no-st16-iconsets` | Skip ST16 iconset conversion in theater MIX files |
| `--shpx` | Convert KeyFrame SHPs to SHPX + `poolnnnn.bin` sidecar (CONQUER / TEMPERAT / DESERT / WINTER) |
| `--shpx-verbose` | Per-shape SHPX/clip log on stderr (requires `--shpx`) |
| `--pool-id ID` | SHPX pool id (default from MIX name: 1–4; requires `--shpx`) |
| `--convert-vqa` | Convert VQA payloads to STVQ |
| `--video-quality Q` | `low` / `medium` / `high` (default `medium`) |
| `--video-effort E` | `fast` / `normal` / `thorough` (default `normal`) |
| `-h`, `--help` | Show help |

### Video presets

| Quality | `cb_per_frame` | Notes |
|---------|----------------|-------|
| low | 32 | Suited for 8 MHz Atari ST |
| medium | 64 | |
| high | 128 | |


| Effort | `dct_coeffs` / chroma | `cb_lookahead` |
|--------|------------------------|----------------|
| fast | 32 / 16 | 0 |
| normal | 48 / 40 | 1 |
| thorough | 60 / 60 | 3 |

`cb_size=2048`, `cb_random_pct=25` stay fixed. Lookahead is extra frames after the current (`0` = current only).

See [spec.md](spec.md) for ST16 scope, detection types (`icn` / `st16` / `vqa` / `stv`), and failure policy.
