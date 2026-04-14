# palette-opt

Imported from **STDOOM** (`HD/STDOOM/palette-opt/`): a small host utility that, given a fixed 256-color palette (`PLAYPAL` in `playpal.c`) and a hard-coded subset of palette indices, exhaustively searches mixing weights for each target color (STDOOM-style dither / distance metric).

`playpal.c` embeds **Command & Conquer `TEMPERAT.PAL`** (768 bytes, VGA 6-bit RGB per channel), extracted from `bin/AtariST/CCLOCAL.MIX` / `TRANSIT.MIX` via `list_mix/list_mix.py -x TEMPERAT.PAL` (same CRC as the retail data file).

Gamma in `main.c` uses **`PLAYPAL[i] / 63.0f`** so 6-bit DAC values are normalized correctly.

### Regenerate Atari c2p weight table

After changing `playpal.c` or `subset[]`:

```bash
make -C tools/palette-opt
python3 tools/palette-opt/gen_c2p_palette_opt_weights_inc.py
```

That rebuilds `ATARILIB/c2p_palette_opt_weights.inc` (256×16 `uint8_t` weights, each row sums to 16) used by `c2p.cpp` for chunky→planar dithering.

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
```

Output is C-style `{ ... }, // index: residual` lines.

## Upstream

To refresh from your STDOOM checkout:

```bash
cp "$STDOOM/palette-opt/main.c" "$STDOOM/palette-opt/playpal.c" tools/palette-opt/
```

Then adjust `subset[]` or `PLAYPAL` in those files as needed.
