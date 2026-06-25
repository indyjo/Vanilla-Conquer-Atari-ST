# palette-opt

Host utility that, given a 256-color palette and a 16-pen subset, searches mixing
weights for each target color (STDOOM-style dither / distance metric) and writes a
C2P `.W16` bundle.

Colors are normalized as **`pal[i] / 63.0f`** before gamma. The default metric is
gamma-corrected **YUV** with **`gamma=1.6`** and **`Y *= 2`**; use `--gamma`,
`--y-scale`, or `--rgb` to change that transform.

### Regenerate Atari c2p weight table

After changing the source palette:

```bash
make -C tools/palette-opt
python3 tools/palette-opt/gen_c2p_palette_opt_weights_inc.py --palette path/to/TEMPERAT.PAL
python3 tools/palette-opt/gen_cps_w16.py   # TITLE/ATTRACT2/SATSEL + CLICK context WSA weights via paltool
```

That rebuilds `ATARILIB/c2p_palette_opt_weights.inc` and `c2p_palette_opt_subset.inc`
(spread subset + 256×16 weights) used by `c2p.cpp` for chunky→planar dithering.

## Build

```bash
cd tools/palette-opt
make
```

Requires a normal C99 compiler and `math` (`-lm`).

## Run

```bash
./palette-opt -o TITLE.W16 -p TITLE.PAL
./palette-opt -p TEMPERAT.PAL --sa-iter=0          # stdout: C-style weight rows
./palette-opt -p TITLE.PAL -o TITLE.W16 --gamma=1.8 --y-scale=1.5
./palette-opt -p TITLE.PAL -o TITLE.W16 --rgb
```

`-p/--palette` is mandatory and must point to a raw 768-byte `.PAL`
(6-bit RGB channels).

If your source palette lives inside `.WSA` or `.CPS`, extract it first with
`tools/paltool`:

```bash
make -C tools/paltool
./tools/paltool/paltool -i SCRSCN1.WSA -o SCRSCN1.PAL
./tools/palette-opt/palette-opt -p SCRSCN1.PAL -o SCRSCN1.W16
```

**`-o FILE`** writes an **4116-byte** `C2P_WeightSet` bundle: magic `W16\0`, 16-byte
`subset[]` (palette index per pen `k`), then `weights[256][16]`. If `FILE` already
exists, the run fails unless **`-c` / `--continue`** (reload subset from `FILE`, then overwrite).

Without **`-o`**, weight rows are printed to stdout in C form; stderr carries logs only.

**Subset (default):** farthest-point spread init, then simulated annealing. Use
**`--sa-iter=0`** to keep the spread (or `--subset-from` / `--subset-init`) subset unchanged.

**Fixed pens:** `--fix PEN,IDX` pins pen `PEN` to palette index `IDX` (repeatable).

**Metric transform:** `--gamma F` sets the gamma exponent, `--y-scale F` scales Y in
YUV space, and `--rgb` switches the optimizer to gamma-corrected RGB space instead of
YUV.

### Subset init and continue

| Init | Flag |
|------|------|
| Spread (default) | (none) |
| Comma list, pen order | `--subset-init=…` |
| Another `.W16` | `--subset-from FILE` |
| Existing `-o` file | `-c` / `--continue` |

`-c` cannot be combined with `--subset-from` or `--subset-init`.

### Simulated annealing

Per index \(i\): \(c_i = (1-\lambda)\,e_1 + \lambda\,e_2\) (Bayer \(e_1\), centroid \(e_2\)).
Global cost \(\sum_i \alpha_i c_i\); without `--hist`, \(\alpha_i = 1/256\).

**Bayer tile / weight granularity:** rows always sum to **16**. Default granularity **1** (full 4×4). **`--bayer=2`** sets granularity **4** (2×2 tile: weights are multiples of 4). Override with **`--weight-granularity=N`** (`N` divides 16). Runtime C2P still uses 4×4 Bayer unless the port is updated separately.

```bash
./palette-opt -p TEMPERAT.PAL -o temperat.w16 --lambda=0.3
./palette-opt -o screen.w16 -p SCREEN.PAL --bayer=2
./palette-opt -p TEMPERAT.PAL -o temperat.w16 --hist counts.txt --sa-iter=12000
./palette-opt -p TEMPERAT.PAL -o temperat.w16 --subset-from baseline.w16 --sa-iter=200
./palette-opt -p TEMPERAT.PAL -o temperat.w16 -c --sa-iter=500
./palette-opt -o SATSEL.W16 -p SATSEL.PAL --sa-iter=0
```

| Flag | Default |
|------|---------|
| `--lambda` | `0.3` |
| `--gamma` | `1.6` |
| `--y-scale` | `2.0` |
| metric | `YUV` (`--rgb` switches to RGB) |
| `--sa-iter` | `100` (`0` = no SA steps) |
| `--sa-log-every` | `10` |
| `--sa-cool` | `0.9995` |
| `--sa-t0` | auto |
| `--sa-seed` | time-based |
| `--bayer` | `4` (granularity 1) |
| `--weight-granularity` | `1` |

Build with OpenMP on Linux (`make OPENMP=1`).

### PLY point clouds

`--dump-ply PREFIX` writes `PREFIX.{palette,subset,mix}.ply` in palette-opt metric space.

```bash
./palette-opt -p TEMPERAT.PAL --dump-ply /tmp/temperat -o temperat.w16
```

### JSON trace (external visualization)

`--export-json FILE` writes a single optimization trace for external visualization and tools
(one `steps[]` entry every `--export-every=N`, default 25). Steps are **streamed to
disk** as optimization runs (unbuffered); each step includes `subset`, `weights`,
`cost`, `best_cost`, `iter_since_best`, and related SA fields.

Use **`--bayer=2`** (2×2 tile, weight granularity 4) for traces and quick tests —
matches the current Atari ST C2P path:

```bash
./palette-opt -p TEMPERAT.PAL -o temperat.w16 --bayer=2 \
  --sa-iter=25000 --sa-seed=1 \
  --export-json temperat_trace.json --export-every=25
```

## Histograms (histtool)

Build a sparse histogram from indexed BMPs and/or existing histogram files, then
pass it to simulated annealing:

```bash
make -C tools/histtool
./tools/histtool/histtool -o ui.hist assets/ui/*.bmp
./tools/palette-opt/palette-opt -o SCREEN.W16 -p SCREEN.PAL --hist ui.hist --sa-iter=12000
```

See [tools/histtool/README.MD](../histtool/README.MD).

## w16fix (subset pen reorder)

After generating a `.W16`, run `tools/w16fix` to pin palette indices `0..15` to
matching hardware pens, then greedily assign the remaining colors using W16
weight-row distance so the strongest remaining low-slot/candidate match is chosen
at each step (weight columns are permuted with the subset):

```bash
make -C tools/w16fix
./tools/w16fix/w16fix -o TITLE.W16 TITLE.W16
```

## Upstream

To refresh from STDOOM: copy `main.c` as needed from `HD/STDOOM/palette-opt/`,
but keep this tree's explicit `-p/--palette` workflow (no built-in palette).
