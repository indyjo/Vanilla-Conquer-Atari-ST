# Atari ST Notes

This file collects Atari ST port specific implementation notes.

## W16 Weightset Format

`*.W16` files are **4116-byte** bundles matching `C2P_WeightSet` in `ATARILIB/c2p.h`:

| Offset | Size | Field |
|--------|------|--------|
| 0 | 4 | Magic `W16\0` (bytes `0x57 0x31 0x36 0x00`) |
| 4 | 16 | `subset[16]` — VGA palette index for each STE hardware pen 0..15 |
| 20 | 4096 | `weights[256][16]` — dither mix weights (row-major) |

Rules:

- Each weight row sums to **16** (4×4 Bayer budget).
- `weights[src][pen]` is the mix weight (0..16) for VGA palette index `src` into hardware pen `pen`.
- `subset[pen]` is the VGA palette index displayed on hardware pen `pen` (used by `Set_Palette` via `C2P_HW_Palette_Subset`).

Theaters and WSAs load the file with `CCFileClass`, validate with `C2P_WeightSet_Validate()`, and install via `C2P_Install_CustomWeights()`. LUTs are baked once during install; the file buffer is not retained. **Legacy 4096-byte weight-only files are not accepted.**

## Runtime Usage

- **Theater play:** `Theater_Atari_TryInstallC2PWeights()` loads `<THEATER>.W16` after the theater `.PAL` is active.
- **WSA playback:** `WSA_Atari_TryInstallC2PWeights()` loads `<basename>.W16` when an animation is opened.

If the file is missing or invalid, the engine keeps the current built-in weight set (`C2P_Clear_CustomWeights()` resets to compiled-in TEMPERAT/HTITLE tables).

## Regenerating W16 Files

Build the host optimizer:

```bash
make -C tools/palette-opt
```

From a WSA with embedded palette (subset defaults to VGA indices 0..15 unless `--subset-spread` is used):

```bash
tools/palette-opt/palette-opt -p path/to/FILE.WSA --dump path/to/FILE.W16
```

From a raw 768-byte theater `.PAL` (recommended: spread subset for optimization):

```bash
tools/palette-opt/palette-opt -p path/to/FILE.PAL --subset-spread \
  --dump path/to/FILE.W16
```

Example (desert theater):

```bash
python3 list_mix/list_mix.py -x DESERT.PAL -o pal_extract_tmp bin/AtariST/DESERT.MIX
tools/palette-opt/palette-opt -p pal_extract_tmp/DESERT.PAL --subset-spread \
  --dump atari-assets/DESERT.W16
```

Ship regenerated `.W16` files from `atari-assets/` next to `cnc.tos` (see `atari-assets/README.md`).
