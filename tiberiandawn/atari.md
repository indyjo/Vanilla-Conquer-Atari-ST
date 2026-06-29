# Atari ST Notes

This file collects Atari ST port specific implementation notes.

## ST16 ICN iconset format

Westwood **ICN iconsets** (the `IControl_Type` blob in `tile.h`) hold terrain stamp graphics (roads, water, slopes, clear, etc.) and other icons such as `TRANS.ICN`. The Atari port adds an optional **ST16** extension for **blitter-ready** ST interleaved 16-color planar icon data.

**File naming:** the format is always the same ICN iconset; only the extension marks the theater (or special case):


| Extension | Theater / use                                   |
| --------- | ----------------------------------------------- |
| `.TEM`    | Temperate terrain iconsets (in `TEMPERAT.MIX`)    |
| `.WIN`    | Winter terrain iconsets (in `WINTER.MIX`)         |
| `.DES`    | Desert terrain iconsets (in `DESERT.MIX`)         |
| `.ICN`    | Theater-independent iconsets (e.g. `TRANS.ICN`) |


Implementation helpers: `atarilib/st16_iconset.h`.

### Standard ICN iconset (Westwood default)

**Unconverted** Westwood iconsets (`Icons = 0x20`, no ST16 chunk): all numeric `IControl_Type` fields (`Width`, `Height`, `Count`, `Size`, `Icons`, tail offsets) are **little-endian (LE)** on disk, matching original PC TD. Read them with `ST16_Read_LE`* / `ST16_Parse_IControl`.

**ST16 iconsets** (ST16 chunk present): numeric fields in `IControl_Type` **and** the ST16 chunk are **big-endian (BE)** — 68000 native order. The four `**ST16` magic bytes at `0x20` are not endian-swapped** (they read as `ST16_MAGIC_NATIVE`, bytes `53 54 31 36`). `**ST16_Has_Native_Chunk`** (ST16 magic + `Icons == 0x2c` in native BE) is the fast indicator that the whole numeric header is BE and may be read directly through `IControl_Type` / `ST16_Chunk_Type` without `le*toh`.


| Offset | Size | Field                                                                                                                                          |
| ------ | ---- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| 0      | 2    | `Width` — icon width in pixels (24 for terrain)                                                                                                |
| 2      | 2    | `Height` — icon height in pixels                                                                                                               |
| 4      | 2    | `Count` — number of **logical** slots in the `Map` table (see below); **not** the number of distinct pixel images stored at `Icons` |
| 6      | 2    | `Allocated` — always 0 on disk                                                                                                                 |
| 8      | 4    | `Size` — total blob size                                                                                                                       |
| 12     | 4    | `Icons` — **0x20** — offset to pixel data                                                                                                      |
| 16     | 4    | `Palettes` — usually 0                                                                                                                         |
| 20     | 4    | `Remaps` — usually 0 (unused in TD runtime)                                                                                                    |
| 24     | 4    | `TransFlag` — offset to per-image transparency bytes (optional)                                                                                |
| 28     | 4    | `Map` — offset to logical slot → physical image index table (optional)                                                                                 |


**Pixel data** at `Icons` (`0x20`): uncompressed **8bpp palette indices**, row-major (`Width × Height` bytes per **physical image**), stored contiguously:

```text
[physical image 0][physical image 1]…[physical image N−1]
```

`N` is the **physical image count** (bytes from `Icons` up to `Map` / `TransFlag`, divided by `Width × Height`). It is usually **≤ `Count`** and is **not** stored as a header field — use `ST16_Icon_Image_Count()` in code. Several logical map slots may reference the same physical image; some slots are empty (`Map[i] = 0xFF`).

Index 0 is a valid color for terrain. Optional tables follow the icon bytes:

- **`Map`**: **`Count` bytes** — logical slot → physical image index (`0xFF` = empty / do not draw). At draw time the engine passes a **logical** stamp index into this table, then blits `Icons + Map[logical] × (Width × Height)`.
- **`TransFlag`**: one byte per **physical image** (table ends at `Map` when present); non-zero means skip palette index 0 when drawing.

### ST16 extension

An ST16 extension header signifies that the iconset is converted for Atari ST's 16-color mode. In this format, the icon pixel data is transformed from classic chunky VGA 8bpp into a true Atari planar layout, matching the system's interleaved 4bpp blitter format. Planar conversion allows direct display on hardware and optionally supports a separate mask plane for per-icon transparency effects.

To detect an ST16 header, examine offset `0x20` for the magic bytes `'ST16'` (`0x53 0x54 0x31 0x36`), or equivalently, check that `Icons` is `0x2c` or higher and validate the chunk structure. The file, when ST16 is present, is stored in big-endian (68000-native) byte order, in contrast to the older Westwood PC iconsets which are little-endian. Presence of the ST16 chunk means all numeric fields—including the initial header and subsequent chunk tables—must be read as big-endian words and longs. This dual-format design allows engines and tools to autodetect and process Atari-specific iconsets efficiently, while remaining backwards-compatible with original Westwood assets.


| `Icons` value | Meaning                                                             |
| ------------- | ------------------------------------------------------------------- |
| `0x20`        | Standard 8bpp iconset (no ST16 chunk)                               |
| `0x28`        | Reserved (not used for TD `Icons`; avoids confusion with RA layout) |
| `≥ 0x2c`      | ST16 present; planar data at `Icons` (v1 uses `**0x2c**`)           |


#### ST16 chunk (12 bytes at offset `0x20`)


| Offset | Size | Field                                                                                    |
| ------ | ---- | ---------------------------------------------------------------------------------------- |
| 0x20   | 4    | Magic `**ST16**` — bytes `0x53 0x54 0x31 0x36` (`ST16_MAGIC_NATIVE` as a 68000 longword) |
| 0x24   | 4    | `size` — payload size, **4** for v1                                                      |
| 0x28   | 2    | `flags` — see below                                                                      |
| 0x2a   | 2    | `reserved` — **0** (ignore on read)                                                      |


**Flags** (v1):


| Bit  | Name       | Meaning                                          |
| ---- | ---------- | ------------------------------------------------ |
| 0    | `HAS_MASK` | Separate 1bpp mask plane after all planar images |
| 1–15 | —          | Reserved, write 0                                |


#### Planar icon data (at `Icons`, typically `0x2c`)

Layout is derived from icon `**Width**`, `**Height**`, and the **physical image count** (not `Count`):

```text
planar_w       = (Width + 15) rounded up to multiple of 16
planar_row     = (planar_w / 16) × 8 bytes
planar_stride  = planar_row × Height
```

Each **physical image** is a tight **ST interleaved 16-color planar** slab (same convention as `st_sprite_cache` / hardware blitter). Bytes are **post-C2P display nibbles** (0–15), not VGA palette indices.

For **24×24** terrain (`planar_w = 32`): **384 bytes** per image (`16 × 24`).

Physical images are stored contiguously:

```text
[planar image 0]…[planar image N−1]
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
+0x2c  planar icons: N × planar_stride          (N = physical image count)
+…     mask icons: N × mask_stride             (only if HAS_MASK; per-icon [planar|mask] packing in convert)
+…     TransFlag / Map                         (Map has Count bytes; same semantics as standard iconset)
```

`TransFlag` and `Map` offsets point past the planar (and mask) region. Terrain tiles are typically unmasked with `TransFlag` unused.

#### Runtime conversion

On first draw of a **24×24 standard iconset** (after theater `.W16` weights are installed via `Init_Theater`), the Atari port converts the MIX-resident blob to ST16 **in place** (`ST16_Convert_InPlace` in `atarilib/st16_convert.cpp` via `ST16_Iconset_Resolve`): chunky pixels are overwritten with planar data, the ST16 chunk is written at `0x20`, numeric header fields are stored in **BE**, and tail tables (`Map` / `TransFlag`) are adjusted; `Size` shrinks. No duplicate copy of the iconset is allocated. Until `C2P_Load_WeightSet` has run, draws use the standard 8bpp stamp path. Conversion is one-way; theater iconsets (`.TEM` / `.WIN` / `.DES`) are theater-specific.

C2P uses **Bayer dither** with `abs_x0=abs_y0=0`, so each icon's top-left pixel pins dither phase `(0,0)` regardless of where the tile is drawn on screen. Each planar slab is zeroed before conversion; columns 24–31 (padding to the 32-pixel blitter width) are cleared afterward so leftover chunky bytes cannot appear as garbage at tile edges.

```text
ST16: converting iconset at <ptr> (<count> icons, 24x24)
ST16: [1/<count>] image 0  8bpp@0x20 -> planar@0x2c
…
ST16: done, Icons=0x2c, Size=<n> (freed <n> bytes)
```

#### Header endianness


| State                                            | Numeric header endianness | How to read                                    |
| ------------------------------------------------ | ------------------------- | ---------------------------------------------- |
| Standard iconset (`Icons = 0x20`, no ST16 chunk) | **LE**                    | `ST16_Read_LE`* / `ST16_Parse_IControl`        |
| ST16 iconset (chunk present)                     | **BE** (native 68000)     | Direct struct access; no `le*toh` on hot paths |


Runtime conversion reads the original LE Westwood blob, then writes back an ST16 blob whose numeric fields are **BE** (`ST16_Native_Swap_Header` during `ST16_Convert_InPlace`). Offline ST16 repack should emit the same BE layout directly. Unconverted blobs in MIX remain LE until converted.

`Buffer_Draw_Stamp` resolves the iconset (converting once if needed), then blits planar data via `ST16_Blit_Stamp`. Unmasked terrain prefers the **hardware blitter** when `HardwareFills=1` in `CONQUER.INI`; otherwise (or if the blit fails) it falls back to a **CPU planar copy**. Masked ST16 stamps still require `HardwareFills`. The old per-tile planar LRU atlas cache has been removed.

Implementation helpers: `atarilib/st16_iconset.h`, `atarilib/st16_draw.h`, `atarilib/st16_convert.h`.

### W16 Weightset Format

`*.W16` files are **4116-byte** bundles matching `C2P_WeightSet` in `ATARILIB/c2p.h`:


| Offset | Size | Field                                                            |
| ------ | ---- | ---------------------------------------------------------------- |
| 0      | 4    | Magic `W16\0` (bytes `0x57 0x31 0x36 0x00`)                      |
| 4      | 16   | `subset[16]` — VGA palette index for each STE hardware pen 0..15 |
| 20     | 4096 | `weights[256][16]` — dither mix weights (row-major)              |


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
- Theater asset MIX files (terrain iconsets use `.TEM`, `.WIN`, or `.DES` inside these archives):
  - `TEMPERAT.MIX` — temperate theater
  - `WINTER.MIX` — winter theater
  - `DESERT.MIX` — desert theater

Additional .MIX files may be loaded based on mission or expansion content, but the ones above are the minimum required for the core campaign.

## Preparing MIX files (Remix Web)

Use **[Remix Web](tools/remix-web/)** — a browser wizard that extracts MIX archives from your own GDI and NOD install discs, merges `GENERAL.MIX`, and repacks with [REMIX](tools/remix/) (11025 Hz audio, even byte offsets).

```bash
make remix-web   # from tiberiandawn/
```

Open `tools/remix-web/web/dist/index.html` via a local static server, or run `npm run dev` in `tools/remix-web/web/` during development. You need both GDI and NOD install disc images (ISO or a ZIP containing one ISO each; volume labels must read `GDI` and `NOD`). Optionally attach the [itch.io release ZIP](https://indyjo.itch.io/commandconquer) to bundle `cnc.tos` and `*.W16` palette weights.

See [tools/remix-web/readme.md](tools/remix-web/readme.md) for full usage.