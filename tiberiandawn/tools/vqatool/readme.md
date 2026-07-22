# vqatool

Host utility for inspecting Westwood VQA files and encoding them to FORM `STVQ`
(`.stv`) for Atari ST. Uses POSIX file I/O only.

See [stvq.md](stvq.md) for the STVQ bitstream.

Typical flow: **inspect** → **init-w16** / **refine-w16** → **fix-w16** → **encode** → **preview**.

## Build

```sh
cd tiberiandawn/tools/vqatool
make
make -C ../palette-opt   # needed for init-w16 / refine-w16
make -C ../w16fix      # needed for fix-w16
```

Sample VQAs (NOD/GDI) live under `samples/` (gitignored). See `samples/readme.md`.

## VQA inspect

```sh
./vqatool inspect GDI1.VQA
./vqatool inspect -f -p -v GDI1.VQA
```

| Option | Meaning |
|--------|---------|
| `-f`, `--frames` | list each frame container and sub-chunk summary |
| `-p`, `--palette` | list frames where the palette changes |
| `-v`, `--verbose` | list every chunk with file offset |

## 16-color W16 sidecars

Per palette segment `N`, files next to the VQA:

| Sidecar | Role |
|---------|------|
| `name.<N>.pal` | VGA6 palette (channels masked to 0..63) |
| `name.<N>.hist` | sparse histogram |
| `name.<N>.w16` | 16-pen subset + C2P weights |

VQA `CPL0`/`CPLZ` bytes often have junk in bits 6–7; vqatool and palette-opt
mask with `& 63` (same as the game’s `Set_Palette` path). Re-run `init-w16` (and
preferably `refine-w16`) after pulling this change so sidecars match.

### init-w16

Write `.pal`/`.hist` (always refreshed) and create missing `.w16` via palette-opt
(`--hist … --sa-iter=0 --fix=0,0`). Existing `.w16` files are left untouched.

```sh
./vqatool init-w16 --palette-opt ../palette-opt/palette-opt \
  [--dry-run] name.vqa
```

### refine-w16

One `palette-opt` invoke per segment: `-c` when `.w16` exists, otherwise a fresh
write. Always passes `--fix=0,0` (pen 0 = palette index 0). Refreshes `.pal`/`.hist`.
Bayer / other SA extras come from palette-opt defaults or `--palette-opt-args`.

```sh
./vqatool refine-w16 --palette-opt ../palette-opt/palette-opt \
  [--normal | --quick | --thorough] \
  [--palette-opt-args=--sa-iter=50000,--sa-seed=1] \
  [--dry-run] name.vqa
```

| Option | Meaning |
|--------|---------|
| `--normal` | SA with `--sa-t0=1e-4` (default) |
| `--quick` | SA with `--sa-t0=1.5e-5` |
| `--thorough` | palette-opt SA defaults |
| `--palette-opt-args=a,b` | extras appended after the preset (gcc `-Wl,a,b` style) |

### fix-w16

Run `tools/w16fix` on each `name.<N>.w16` in place (no `-o`). Reorders subset
pens `0..15` and permutes weight columns. Requires existing `.w16`.

```sh
./vqatool fix-w16 --w16fix ../w16fix/w16fix [--dry-run] name.vqa
```

## STVQ encode / preview

### encode

Requires existing `name.<N>.w16` sidecars; aborts if any are missing.

```sh
./vqatool encode \
  [--cb-size 2048] [--cb-per-frame 32] [--cb-random-pct 25] [--cb-lookahead 1] \
  [--gamma 0.77] [--dct-alpha 0.2] [--dct-coeffs 15] [--dct-chroma-coeffs 7] \
  [-o out.stv] [--dry-run] name.vqa
```

Defaults: `--cb-size 2048`, `--cb-per-frame 32`, `--cb-random-pct 25`, `--cb-lookahead 1`,
`--gamma 0.77`, `--dct-alpha 0.2`, `--dct-coeffs 15`, `--dct-chroma-coeffs 7`.
STCR: residual-only shortlist of `2*R` (stay-as-is aware); accept `(100-random)%` by
add-utility (ignore eviction) paired with least-damage victims (ignore install); then
accept `random%` tiles directly. On palette/segment changes, codebook DCT features are
recomputed under the new W16 and every existing CB entry becomes a **prime eviction
candidate**; STCR always replaces the lowest-index free prime before damage-based
victims (normal `R` / random%). STVD assignment and N−2 skip never use pre-cut tiles:
only current-epoch CB entries are referenced, and the cut frame is force-full. Tile error is weighted **YUV-DCT** feature L2: source
and recon → palette-opt YUV (`--gamma`) → per-plane 8×8 DCT → zig-zag packs
`[Y×Ny | U×Nc | V×Nc]` with `√(1/(1+α(u²+v²)))`. Fewer chroma coeffs underweight U/V
vs Y (chroma is smoother; U/V magnitudes are already smaller than Y).

STCR lookahead spans at most one palette cut: pre-cut frames score under the old palette,
post-cut under the new; a second cut shrinks the window.

### preview

Decode a `.stv` and pipe RGB24 + signed 8-bit PCM into ffmpeg (`pipe()` + `/dev/fd/N`,
no FIFOs):

```sh
./vqatool preview [--ffmpeg PATH] [-o out.mkv] name.stv
```

Defaults: `$FFMPEG` or `ffmpeg` on `PATH`; output `name.mkv`. Video: H.264
(`libx264`, ~1s keyframes). Audio: STVQ `s8` → AAC.
