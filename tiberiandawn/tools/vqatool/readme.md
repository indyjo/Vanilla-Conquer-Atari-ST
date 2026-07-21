# vqatool

Host utility for inspecting Westwood VQA files and encoding them to FORM `STVQ`
(`.stv`) for Atari ST. Uses POSIX file I/O only.

See [stvq.md](stvq.md) for the STVQ bitstream.

Typical flow: **inspect** → **init-w16** (optional **refine-w16**) → **encode** → **preview**.

## Build

```sh
cd tiberiandawn/tools/vqatool
make
make -C ../palette-opt   # needed for init-w16 / refine-w16
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
| `name.<N>.pal` | VGA6 palette |
| `name.<N>.hist` | sparse histogram |
| `name.<N>.w16` | 16-pen subset + C2P weights |

### init-w16

Write `.pal`/`.hist` (always refreshed) and create missing `.w16` via palette-opt
(`--hist … --sa-iter=0 --bayer=2`). Existing `.w16` files are left untouched.

```sh
./vqatool init-w16 --palette-opt ../palette-opt/palette-opt \
  [--dry-run] name.vqa
```

### refine-w16

Continue-refine existing `.w16` with palette-opt (`-c`). Requires matching
`.pal` / `.hist` (from `init-w16`).

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

## STVQ encode / preview

### encode

Requires existing `name.<N>.w16` sidecars; aborts if any are missing.

```sh
./vqatool encode \
  [--cb-size 2048] [--cb-per-frame 32] [--cb-random-pct 25] [--cb-lookahead 1] \
  [--gamma 0.77] [--y-scale 2] [--dct-alpha 0.2] [--dct-coeffs 15] \
  [-o out.stv] [--dry-run] name.vqa
```

Defaults: `--cb-size 2048`, `--cb-per-frame 32`, `--cb-random-pct 25`, `--cb-lookahead 1`,
`--gamma 0.77`, `--y-scale 2`, `--dct-alpha 0.2`, `--dct-coeffs 15`.
STCR: residual-only shortlist of `2*R` (stay-as-is aware); accept `(100-random)%` by
add-utility (ignore eviction) paired with least-damage victims (ignore install); then
accept `random%` tiles directly. Tile error is weighted **Y-DCT** feature L2: source and
recon → palette-opt YUV (`--gamma` / `--y-scale`) → 8×8 DCT → first `N` zig-zag coeffs ×
`√(1/(1+α(u²+v²)))`.

### preview

Decode a `.stv` and pipe RGB24 + signed 8-bit PCM into ffmpeg (`pipe()` + `/dev/fd/N`,
no FIFOs):

```sh
./vqatool preview [--ffmpeg PATH] [-o out.mkv] name.stv
```

Defaults: `$FFMPEG` or `ffmpeg` on `PATH`; output `name.mkv`. Video: H.264
(`libx264`, ~1s keyframes). Audio: STVQ `s8` → AAC.
