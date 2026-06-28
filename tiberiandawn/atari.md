# Atari ST Notes

This file collects Atari ST port specific implementation notes.

## ST16 ICN / TEM Iconset Format

Template terrain tiles (`.TEM` in theater MIX files) and icon stamps (`.ICN`, e.g. `TRANS.ICN`) use a Westwood **`IControl_Type`** header defined in `tile.h`. The Atari port adds an optional **ST16** extension for **blitter-ready** ST interleaved 16-color planar icon data.

Implementation helpers: `atarilib/st16_iconset.h`.

### Standard ICN (Westwood default)

| Offset | Size | Field |
|--------|------|--------|
| 0 | 2 | `Width` — icon width in pixels (24 for terrain) |
| 2 | 2 | `Height` — icon height in pixels |
| 4 | 2 | `Count` — number of distinct icon images in the set |
| 6 | 2 | `Allocated` — always 0 on disk |
| 8 | 4 | `Size` — total blob size |
| 12 | 4 | `Icons` — **0x20** — offset to pixel data |
| 16 | 4 | `Palettes` — usually 0 |
| 20 | 4 | `Remaps` — usually 0 (unused in TD runtime) |
| 24 | 4 | `TransFlag` — offset to per-image transparency bytes (optional) |
| 28 | 4 | `Map` — offset to template cell → image index table (optional) |

**Pixel data** at `Icons` (`0x20`): uncompressed **8bpp palette indices**, row-major (`Width × Height` bytes per image), stored contiguously:

```text
[image 0][image 1]…[image Count−1]
```

Index 0 is a valid color for terrain. Optional tables follow the icon bytes:

- **`TransFlag`**: `Count` bytes; non-zero means skip palette index 0 when drawing.
- **`Map`**: `template_W × template_H` bytes; maps logical cell slot → image index (`0xFF` = empty cell).

### ST16 extension

When planar ST data is present, the header is unchanged for the first 32 bytes except **`Icons`** and **`Size`**. A fixed **12-byte** chunk is inserted at offset **`0x20`**; planar pixels start at **`0x2c`**.

| `Icons` value | Meaning |
|---------------|---------|
| `0x20` | Standard 8bpp ICN (no ST16 chunk) |
| `0x28` | Reserved (not used for TD `Icons`; avoids confusion with RA layout) |
| `≥ 0x2c` | ST16 present; planar data at `Icons` (v1 uses **`0x2c`**) |

#### ST16 chunk (12 bytes at offset `0x20`)

| Offset | Size | Field |
|--------|------|--------|
| 0x20 | 4 | Magic **`ST16`** — bytes `0x53 0x54 0x31 0x36`, LE `0x36315453` |
| 0x24 | 4 | `size` — payload size, **4** for v1 |
| 0x28 | 2 | `flags` — see below |
| 0x2a | 2 | `reserved` — **0** (ignore on read) |

**Flags** (v1):

| Bit | Name | Meaning |
|-----|------|---------|
| 0 | `HAS_MASK` | Separate 1bpp mask plane after all planar images |
| 1–15 | — | Reserved, write 0 |

#### Planar icon data (at `Icons`, typically `0x2c`)

Layout is derived from `Width`, `Height`, and `Count` (no extra header fields):

```text
planar_w       = (Width + 15) rounded up to multiple of 16
planar_row     = (planar_w / 16) × 8 bytes
planar_stride  = planar_row × Height
```

Each image is a tight **ST interleaved 16-color planar** slab (same convention as `st_sprite_cache` / hardware blitter). Bytes are **post-C2P display nibbles** (0–15), not VGA palette indices.

For **24×24** terrain (`planar_w = 32`): **384 bytes** per image (`16 × 24`).

All images are stored contiguously:

```text
[planar image 0]…[planar image Count−1]
```

#### Optional mask plane (`HAS_MASK`)

When bit 0 of `flags` is set:

```text
mask_row    = (planar_w / 16) × 2 bytes
mask_stride = mask_row × Height
```

Masks follow all planar images. **1 bpp**, MSB = leftmost pixel in each 16-column group; bit **1** preserves backdrop, bit **0** applies color (sprite/mouse convention).

For **24×24**: **96 bytes** per mask image (`4 × 24`).

#### Full ST16 blob layout

```text
+0x00  IControl_Type (32 bytes), Icons = 0x2c, Size updated
+0x20  ST16 chunk (12 bytes)
+0x2c  planar icons: Count × planar_stride
+…     mask icons: Count × mask_stride   (only if HAS_MASK)
+…     TransFlag / Map                    (same semantics as standard ICN)
```

`TransFlag` and `Map` offsets point past the planar (and mask) region. Terrain tiles are typically unmasked with `TransFlag` unused.

#### Runtime conversion

On first draw of a **24×24 standard ICN** template (after theater `.W16` weights are installed via `Init_Theater`), the Atari port converts the MIX-resident blob to ST16 **in place** (`ST16_Convert_InPlace` in `atarilib/st16_convert.cpp` via `ST16_Iconset_Resolve`): chunky pixels are overwritten with planar data, the header and tail (`Map` / `TransFlag`) are adjusted, and `Size` shrinks. No duplicate copy of the iconset is allocated. Until `C2P_Load_WeightSet` has run, draws use the standard 8bpp stamp path. Conversion is one-way; theater tiles are theater-specific.

C2P uses **Bayer dither** with `abs_x0=abs_y0=0`, so each icon's top-left pixel pins dither phase `(0,0)` regardless of where the tile is drawn on screen. Each planar slab is zeroed before conversion; columns 24–31 (padding to the 32-pixel blitter width) are cleared afterward so leftover chunky bytes cannot appear as garbage at tile edges.

```text
ST16: converting iconset at <ptr> (<count> icons, 24x24)
ST16: [1/<count>] image 0  8bpp@0x20 -> planar@0x2c
…
ST16: done, Icons=0x2c, Size=<n> (freed <n> bytes)
```

`Buffer_Draw_Stamp` resolves the iconset (converting once if needed), then blits planar data via `ST16_Blit_Stamp`. Unmasked terrain prefers the **hardware blitter** when `HardwareFills=1` in `CONQUER.INI`; otherwise (or if the blit fails) it falls back to a **CPU planar copy**. Masked ST16 stamps still require `HardwareFills`. The old per-tile planar LRU atlas cache has been removed.

Implementation helpers: `atarilib/st16_iconset.h`, `atarilib/st16_draw.h`, `atarilib/st16_convert.h`.

### W16 Weightset Format

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

From a WSA with embedded palette:

```bash
tools/palette-opt/palette-opt -p path/to/FILE.WSA -o path/to/FILE.W16 --sa-iter=0
```

From a raw 768-byte theater `.PAL` (spread init, no SA; add `--sa-iter=N` to optimize):

```bash
tools/palette-opt/palette-opt -p path/to/FILE.PAL -o path/to/FILE.W16 --sa-iter=0
```

Example (desert theater):

```bash
python3 list_mix/list_mix.py -x DESERT.PAL -o pal_extract_tmp bin/AtariST/DESERT.MIX
tools/palette-opt/palette-opt -p pal_extract_tmp/DESERT.PAL \
  -o atari-assets/DESERT.W16 --sa-iter=0
```

Ship regenerated `.W16` files from `atari-assets/` next to `cnc.tos` (see `atari-assets/README.md`).

## Required MIX files
Put these mix files next to the CNC.TOS executable:
The following MIX files must be present in the same directory as `CNC.TOS`. These are loaded by the game at runtime:

- `CONQUER.MIX`    — base game assets (maps, graphics, palette)
- `GENERAL.MIX`    — support assets (sidebar icons, overlays, etc)
- `SCORES.MIX`     — music tracks (in .AUD format)
- `SOUNDS.MIX`     — sound effects (.AUD and .V00)
- `SPEECH.MIX`     — EVA speech lines
- Theater asset MIX files:  
  - `TEMPERAT.MIX` — temperate theater graphics
  - `SNOW.MIX`     — snow theater graphics
  - `WINTER.MIX`   — winter theater graphics
  - `DESERT.MIX`   — desert theater graphics

Additional .MIX files may be loaded based on mission or expansion content, but the ones above are the minimum required for the core campaign.

## Preparing MIX files (Remix Web)

Use **[Remix Web](tools/remix-web/)** — a browser wizard that extracts MIX archives from your own GDI and NOD install discs, merges `GENERAL.MIX`, and repacks with [REMIX](tools/remix/) (11025 Hz audio, even byte offsets).

```bash
make remix-web   # from tiberiandawn/
```

Open `tools/remix-web/web/dist/index.html` via a local static server, or run `npm run dev` in `tools/remix-web/web/` during development. You need both GDI and NOD install disc images (ISO or a ZIP containing one ISO each; volume labels must read `GDI` and `NOD`). Optionally attach the [itch.io release ZIP](https://indyjo.itch.io/commandconquer) to bundle `cnc.tos` and `*.W16` palette weights.

See [tools/remix-web/readme.md](tools/remix-web/readme.md) for full usage.
