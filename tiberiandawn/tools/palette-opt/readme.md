# palette-opt

Host utility that, given a fixed 256-color palette (`PLAYPAL` in `playpal.c`) and a 16-pen subset, searches mixing weights for each target color (STDOOM-style dither / distance metric) and writes a C2P `.W16` bundle.

`playpal.c` embeds **Command & Conquer `TEMPERAT.PAL`** (768 bytes, VGA 6-bit RGB per channel).

Gamma uses **`PLAYPAL[i] / 63.0f`** so 6-bit DAC values are normalized correctly.

### Regenerate Atari c2p weight table

After changing `playpal.c`:

```bash
make -C tools/palette-opt
python3 tools/palette-opt/gen_c2p_palette_opt_weights_inc.py
python3 tools/palette-opt/gen_cps_w16.py   # TITLE/ATTRACT2/SATSEL + CLICK context WSA weights
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
./palette-opt -o SCRSCN1.W16 -p SCRSCN1.WSA
./palette-opt -p TEMPERAT.PAL --sa-iter=0          # stdout: C-style weight rows
```

`-p/--palette` accepts either:
- raw 768-byte `.PAL` (6-bit RGB channels), or
- `.WSA` with embedded palette.

**`-o FILE`** writes an **4116-byte** `C2P_WeightSet` bundle: magic `W16\0`, 16-byte
`subset[]` (palette index per pen `k`), then `weights[256][16]`. If `FILE` already
exists, the run fails unless **`-c` / `--continue`** (reload subset from `FILE`, then overwrite).

Without **`-o`**, weight rows are printed to stdout in C form; stderr carries logs only.

**Subset (default):** farthest-point spread init, then simulated annealing. Use
**`--sa-iter=0`** to keep the spread (or `--subset-from` / `--subset-init`) subset unchanged.

**Fixed pens:** `--fix PEN,IDX` pins pen `PEN` to palette index `IDX` (repeatable).

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
./palette-opt -o temperat.w16 --lambda=0.3
./palette-opt -o screen.w16 -p SCREEN.PAL --bayer=2
./palette-opt -o temperat.w16 --hist counts.txt --sa-iter=12000
./palette-opt -o temperat.w16 --subset-from baseline.w16 --sa-iter=200
./palette-opt -o temperat.w16 -c --sa-iter=500
./palette-opt -o SATSEL.W16 -p SATSEL.PAL --sa-iter=0
```

| Flag | Default |
|------|---------|
| `--lambda` | `0.3` |
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
matching hardware pens and pack the rest (`>= 16`) in ascending palette-index order
(weight columns are permuted with the subset):

```bash
make -C tools/w16fix
./tools/w16fix/w16fix -o TITLE.W16 TITLE.W16
```

## Upstream

To refresh from STDOOM: copy `main.c` / `playpal.c` from `HD/STDOOM/palette-opt/`.
