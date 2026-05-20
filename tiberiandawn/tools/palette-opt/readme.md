# palette-opt

Imported from **STDOOM** (`HD/STDOOM/palette-opt/`): a small host utility that, given a fixed 256-color palette (`PLAYPAL` in `playpal.c`) and a hard-coded subset of palette indices, exhaustively searches mixing weights for each target color (STDOOM-style dither / distance metric).

`playpal.c` embeds **Command & Conquer `TEMPERAT.PAL`** (768 bytes, VGA 6-bit RGB per channel), extracted from `bin/AtariST/CCLOCAL.MIX` / `TRANSIT.MIX` via `list_mix/list_mix.py -x TEMPERAT.PAL` (same CRC as the retail data file).

Gamma in `main.c` uses **`PLAYPAL[i] / 63.0f`** so 6-bit DAC values are normalized correctly.

### Regenerate Atari c2p weight table

After changing `playpal.c`:

```bash
make -C tools/palette-opt
python3 tools/palette-opt/gen_c2p_palette_opt_weights_inc.py
python3 tools/palette-opt/gen_cps_w16.py   # TITLE/ATTRACT2/SATSEL + CLICK context WSA weights
```

That rebuilds `ATARILIB/c2p_palette_opt_weights.inc` and `c2p_palette_opt_subset.inc`
(spread subset + 256×16 weights, each row sums to 16) used by `c2p.cpp` for chunky→planar dithering.

This tree does not use it in the build yet; it is kept for experimentation (e.g. Atari ST chunky-to-planar or palette reduction work).

## Build

```bash
cd tools/palette-opt
make
```

Requires a normal C99 compiler and `math` (`-lm`).

## Run

```bash
./palette-opt           # all 256 entries
./palette-opt 0 16      # entries 0..15 only
./palette-opt -p SOME.PAL --dump some.w16
./palette-opt -p SCRSCN1.WSA --dump SCRSCN1.W16
```

`-p/--palette` accepts either:
- raw 768-byte `.PAL` (6-bit RGB channels), or
- `.WSA` with embedded palette (uses the first 16 palette indices as the ST subset).

`--dump out.w16` writes an **4116-byte** `C2P_WeightSet` bundle: magic `W16\0`, 16-byte
`subset[]` (VGA index per hardware pen), then `weights[256][16]`. Use `--subset-spread`
to pick the subset via farthest-point sampling; without it, indices 0..15 are used.

### Subset selection (16 ST pens)

`--subset-spread` picks 16 indices with **farthest-point sampling** in the same scaled
YUV space as the weight optimizer (`subset_spread.c`). Each new pen maximizes the minimum
distance to pens already chosen.

```bash
./palette-opt --subset-spread --dump-ply temperat_spread
```

### PLY point clouds (CloudCompare / MeshLab)

`--dump-ply PREFIX` writes ASCII Stanford PLY files in palette-opt metric space
`(x,y,z) = (2*y, u, v)` with VGA RGB and a `palette_index` property:

| File | Contents |
|------|----------|
| `PREFIX.palette.ply` | All 256 source palette colors |
| `PREFIX.subset.ply` | Current ST subset pens (`subset[]`) |
| `PREFIX.mix.ply` | Per-index dither mix centroids (after weight pass) |

```bash
./palette-opt --dump-ply /tmp/temperat -p TEMPERAT.PAL
./palette-opt --subset-spread --dump-ply /tmp/temperat -p TEMPERAT.PAL
```

`--subset-spread-only` with `--dump-ply` writes palette + subset only (no mix).

Console output remains C-style `{ ... }, // index: residual` lines.

## Upstream

To refresh from your STDOOM checkout:

```bash
cp "$STDOOM/palette-opt/main.c" "$STDOOM/palette-opt/playpal.c" tools/palette-opt/
```

Then adjust `subset[]` or `PLAYPAL` in those files as needed.
