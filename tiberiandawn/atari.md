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

**Offline repack:** the `remix` tool (host CLI, `remix.tos`, and [remix-web](../tools/remix-web/)) can convert standard iconsets in theater MIX files to ST16 during MIX repack when matching `*.W16` weights are available. This avoids mission-start conversion and reduces MIX size. See [tools/remix/spec.md](../tools/remix/spec.md).

`Buffer_Draw_Stamp` resolves the iconset (converting once if needed), then blits planar data via `ST16_Blit_Stamp`. Unmasked terrain prefers the **hardware blitter** when `HardwareFills=1` in `CONQUER.INI`; otherwise (or if the blit fails) it falls back to a **CPU planar copy**. Masked ST16 stamps still require `HardwareFills`. The old per-tile planar LRU atlas cache has been removed.

Implementation helpers: `atarilib/st16_iconset.h`, `atarilib/st16_draw.h`, `atarilib/st16_convert.h`.

## SHPX external-pool KeyFrame SHP format

Westwood **KeyFrame SHP** blobs (`E1.SHP`, `HTNK.SHP`, `RADAR.GDI`, etc.) pack animation metadata, a per-frame offset table, and **compressed 8bpp** payload (LCW keyframes, XOR delta chains) into one MIX entry. The Atari port defines **SHPX** as a metadata-only variant: the SHPX entry holds the KeyFrame header, **table offsets**, the **KeyFrame frame table** (big-endian), and auxiliary tables; **compressed pixel bytes live in an external pool** referenced by a 16-bit pool identifier.

This is **not** the classic per-shape `ShapeBlock_Type` format used by `MOUSE.SHP`.

**Goals:**

- Keep **compressed** payload on disk and in RAM (no 4bpp planar in the pool — planar expansion stays in a dynamic decode/C2P cache).
- **Retain** the original overlapping frame table and XOR delta chain semantics (`Build_Frame` in `keyframe.cpp`); only endianness and payload addressing change.
- Declare a **`pool_data_begin` / `pool_data_size`** span so the runtime can load and **cache each animation’s pool slice** in RAM (or the whole MIX sidecar once per `pool_id`).
- Store **precomputed clip rectangles** in a separate table; planar row stride and mask layout are derived at runtime in the sprite cache.

**Detection:** the first four bytes are the magic **`SHPX`** (bytes `0x53 0x48 0x50 0x58`). On the 68000, read a single **native longword** at offset `0` and compare to **`SHPX_MAGIC_NATIVE`** (`0x53485058`); the magic is not endian-swapped. Do not use `memcmp`. Host tools reading LE file bytes may compare against `SHPX_MAGIC` (`0x58504853`). Run this check before monolithic KeyFrame SHP heuristics (which look at `frames` + in-blob offsets).

### SHPX blob layout (v1)

```text
+0x00  magic 'SHPX'                         (4 bytes, literal — not endian-swapped)
+0x04  KeyFrame header                      (14 bytes, numeric fields BE)
+0x12  pool_id                              (u16 BE)
+0x14  reserved                             (u16 BE, must be 0)
+0x16  frame_table_offset                   (u32 BE — byte offset from blob start)
+0x1A  clip_table_offset                    (u32 BE)
+0x1E  pool_data_begin                      (u32 BE — byte offset in external pool)
+0x22  pool_data_size                       (u32 BE — bytes in cacheable span)
+…     tables and padding (offsets may point anywhere within the blob)
       (no embedded palette, no in-blob compressed payload)
```

Fixed SHPX prefix: **38 bytes** (`0x26`). Tables are usually packed contiguously after the prefix but may be reordered; only the offset fields are authoritative.

### KeyFrame header (14 bytes at `0x04`)

Same fields as `KeyFrameHeaderType` in `keyframe.cpp` / `common/keyframe.h`. All multi-byte values are **big-endian (BE)** — 68000 native order on disk.


| Offset | Size | Field                 | Notes                                                          |
| ------ | ---- | --------------------- | -------------------------------------------------------------- |
| +0     | 2    | `frames`              | Number of animation frames                                     |
| +2     | 2    | `x`                   | Hotspot X (draw anchor in logical frame space)                 |
| +4     | 2    | `y`                   | Hotspot Y                                                      |
| +6     | 2    | `width`               | Logical frame width (pixels); same for every frame               |
| +8     | 2    | `height`              | Logical frame height                                           |
| +10    | 2    | `largest_frame_size`  | Max LCW/XOR decompress workspace (same meaning as monolithic KeyFrame SHP)  |
| +12    | 2    | `flags`               | v1: **no embedded palette** — bit 0 must be 0; other bits reserved |


Monolithic KeyFrame SHPs store these fields **little-endian** and omit the `SHPX` magic. The frame table always starts at byte **14** immediately after the header.

### Pool reference (`0x12`)


| Offset | Size | Field     | Notes                                                                 |
| ------ | ---- | --------- | --------------------------------------------------------------------- |
| +0     | 2    | `pool_id` | **Shared by all SHPX entries from the same MIX** (BE). `0` is reserved / invalid. |
| +2     | 2    | `reserved`| Must be **0** on write; ignore on read.                               |


**`pool_id` assignment (repack):** one id per eligible MIX; all SHPX entries in that MIX share it. `0` is reserved / invalid.

| MIX | `pool_id` | Sidecar |
|-----|-----------|---------|
| `CONQUER.MIX` | `0x0001` | `pool0001.bin` |
| `TEMPERAT.MIX` | `0x0002` | `pool0002.bin` |
| `DESERT.MIX` | `0x0003` | `pool0003.bin` |
| `WINTER.MIX` | `0x0004` | `pool0004.bin` |

**Sidecar pool file:** one binary pool beside the MIX, named from that id:

```text
pool%04x.bin    (lowercase hex, 8.3-friendly)
```

The sidecar lives in the **same directory** as the MIX (not inside the MIX). Other MIX files keep monolithic KeyFrame SHPs.

All SHPX entries converted from the same MIX share the **same** `pool_id` and the **same** sidecar file. Each entry has its own **`pool_data_begin` / `pool_data_size`** slice within that file.

### Pool data span (`0x1E`)

Describes the **contiguous byte range** within the MIX sidecar pool that **this animation** uses. The runtime may cache **`pool_data_size` bytes at `pool_data_begin`** per shape, or map/cache the **entire** sidecar once per `pool_id` (implementation choice).


| Offset | Field             | Notes |
| ------ | ----------------- | ----- |
| `0x1E` | `pool_data_begin` | Byte offset from the **start of the sidecar pool file** (BE). **Must be even.** Each SHP’s slice starts at an even offset in the shared pool. |
| `0x22` | `pool_data_size`  | Length of this animation’s payload span (BE). Covers every LCW/XOR byte referenced by its frame table (payload tail copy, offsets rebased to `0` within the slice). **`0`** is reserved / invalid. Padding bytes between slices are **not** included in `pool_data_size`. |


**Payload addressing:** every **24-bit value** in the frame table that points into compressed data is relative to **`pool_data_begin`**, not pool byte `0` and not the SHPX blob:

```text
payload_ptr = pool_cache_base + (offset & 0xFFFFFF)
```

where `pool_cache_base` is the in-RAM copy of `pool[pool_data_begin … pool_data_begin + pool_data_size)`.

Monolithic KeyFrame SHPs used the same numeric offsets but **absolute from byte 0 of the SHP blob** (`dataptr + offset` in `Build_Frame`). Repack copies each shape’s payload tail into the shared sidecar at **`pool_data_begin`** and rewrites frame-table offsets as **`source_offset − tail_start`** within that slice.

### Table offsets (`0x16`)

All offsets are **byte positions from the start of the SHPX blob** (include the `SHPX` magic). Each value is a **u32 BE**.


| Offset | Field                   | Points to |
| ------ | ----------------------- | --------- |
| `0x16` | `frame_table_offset`    | KeyFrame frame table (see below) |
| `0x1A` | `clip_table_offset`     | Clip table (see below) |


### Frame table (KeyFrame layout, big-endian)

The frame table is **byte-for-byte the same structure** as the monolithic KeyFrame SHP frame table: **`8 × frames` bytes** at `frame_table_offset`.

Each frame slot occupies **8 bytes** on disk, but `Build_Frame` reads **three u32 values** (12 bytes) starting at `frame_table_offset + N×8`, so the third longword **overlaps** the next frame’s slot:

```text
Frame 0:  [AAAA][BBBB]
Frame 1:       [BBBB][CCCC]
Frame 2:            [CCCC][DDDD]
          …
```

Each u32 is stored **big-endian** (opposite of the original little-endian monolithic format). Bit layout per longword is unchanged:

```text
 31      24 23                    0
┌──────────┬──────────────────────┐
│ KF flags │ 24-bit value         │
└──────────┴──────────────────────┘
```

- **High byte:** `KF_KEYFRAME`, `KF_KEYDELTA`, `KF_DELTA`, … (`KeyFrameType` in `common/keyframe.h`) on the **first** longword of each 8-byte slot.
- **Low 24 bits:** offset into the **cacheable pool span** (LCW data or XOR patch), **relative to `pool_data_begin`**, or a **reference frame index** for `KF_DELTA` rows (same rules as `Build_Frame` — frame indices are not pool offsets).

`Build_Frame` delta-chain walking (reload up to seven longwords from overlapping rows, `SUBFRAMEOFFS = 7`) applies unchanged; the table base is `frame_table_offset`, longwords are **BE**, and payload pointers use the cached pool slice as described above.

### Clip table

**`8 × frames` bytes** at `clip_table_offset`. One row per frame, independent of the overlapping frame table:


| Offset | Size | Field    | Notes |
| ------ | ---- | -------- | ----- |
| +0     | 2    | `clip_x` | BE u16; logical `width × height` coordinates |
| +2     | 2    | `clip_y` | BE u16 |
| +4     | 2    | `clip_w` | BE u16; **`0` = empty frame** |
| +6     | 2    | `clip_h` | BE u16 |


**Clip semantics:** minimal axis-aligned bounds of non-transparent pixels after decode, with palette index **0** as transparent (same as `st_sprite_cache` crop scan). Header `width` / `height` remain the full logical frame size.

### External pool (sidecar, compressed payload)

The sidecar holds the **same compressed byte streams** that previously lived after each shape’s frame table inside monolithic KeyFrame SHPs — LCW keyframes, XOR delta data, unchanged encoding. It does **not** hold decoded 8bpp pixels or 4bpp planar data.

**Repack (`remix` CLI / remix-web; CONQUER / TEMPERAT / DESERT / WINTER):**

1. Detect **KeyFrame** SHPs (`flags` / frame table heuristics; skip classic `ShapeBlock` / `MOUSE.SHP`).
2. For each shape, take the **payload tail** (bytes after the frame table, rebased so the slice starts at logical offset `0` inside that tail).
3. Append each tail contiguously into that MIX’s sidecar (`pool0001.bin` … `pool0004.bin` per the table above).
4. Record **`pool_data_begin`** (even file offset) and **`pool_data_size`** in the SHPX header; rewrite frame-table 24-bit offsets as **`source_offset − tail_start`** (big-endian).
5. Replace the MIX entry **in place** (same name, e.g. `E1.SHP`, payload is SHPX metadata).
6. Build **clip table** offline (`Build_Frame` + tight bbox, index `0` transparent).

**Even alignment in the sidecar:** if a shape’s payload size is **odd**, append one **`0x00` pad byte** after it in the pool file before placing the next shape’s payload, so **no payload starts on an odd file offset**. (Helps hosts that mmap the pool at even addresses; TOS loaders may still copy into ST-RAM.) Pad bytes are **not** part of any shape’s `pool_data_size` and must not be referenced by frame tables.

XOR delta chains stay as-is inside each slice; pointers remain **within that shape’s slice**, never cross-shape.

SHPX does not define a magic header on the sidecar file — it is a raw concatenation of payload slices and optional pad bytes.

### Runtime

1. Read native longword at offset 0 and compare to **`SHPX_MAGIC_NATIVE`** (`0x53485058`).
2. Cast to **`ShpxPrefix`** and read header/table fields with native **`uint16_t` / `uint32_t`** access (68000 big-endian matches on-disk layout).
3. On each **`Build_Frame`** for an SHPX shape: **`Alloc`** a buffer of **`pool_data_size`**, **`SHPX_Pool_Read_Slice`** loads that span from **`pool%04x.bin`** at **`pool_data_begin`**, decode runs from the slice + metadata, then **`Free`** the buffer.
4. Clip table is available via `SHPX_Get_Frame_Clip`; the planar sprite cache uses it on LRU miss (no transparent-pixel crop scan for SHPX).

No whole-pool RAM cache at startup — payload is read per decode. Sidecars must stay on disk beside their MIX files for the session.

Implementation: `atarilib/shpx.cpp` (`SHPX_Pool_Read_Slice`), `keyframe.cpp` (`Build_Frame` SHPX dispatch).

### KeyFrame SHP vs SHPX


| Aspect              | KeyFrame SHP (monolithic)        | SHPX v1                                      |
| ------------------- | -------------------------------- | -------------------------------------------- |
| Magic               | None                             | `'SHPX'` at offset 0                         |
| Header endianness   | LE                               | BE                                           |
| Frame table         | 8 bytes/frame, overlapping u32 reads at +14 | Same layout at `frame_table_offset`, BE |
| Payload offsets     | Absolute from **SHP blob** byte 0 | Relative to **`pool_data_begin`** in cached pool slice |
| Delta / XOR chains  | Yes                              | Yes (same `Build_Frame` logic)               |
| Payload             | In same blob after table         | Sidecar `pool%04x.bin` beside the source MIX |
| Cacheable span      | Implicit (whole MIX entry)       | Per-shape **`pool_data_begin` + `pool_data_size`** in shared sidecar |
| Clip metadata       | None (runtime scan)              | Separate clip table                          |
| Embedded palette    | Optional (`flags & 1`)           | Not supported in v1                          |
| Planar stride/mask  | N/A                              | Runtime cache only                           |

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

- `CONQUER.MIX`    — game sprites and shapes (units, buildings, sidebar icons, effects; KeyFrame SHPs → SHPX)
- `pool0001.bin`   — SHPX pool for `CONQUER.MIX` (required when using repacked SHPX `CONQUER.MIX`)
- `pool0002.bin`   — SHPX pool for `TEMPERAT.MIX` (when using repacked SHPX theater MIX)
- `pool0003.bin`   — SHPX pool for `DESERT.MIX`
- `pool0004.bin`   — SHPX pool for `WINTER.MIX`
- `GENERAL.MIX`    — cutscenes, mission data, title screens (WSA, CPS, INI, BIN)
- `SCORES.MIX`     — music tracks (in .AUD format)
- `SOUNDS.MIX`     — sound effects (.AUD and .V00)
- `SPEECH.MIX`     — EVA speech lines
- Theater asset MIX files (terrain iconsets use `.TEM`, `.WIN`, or `.DES` inside these archives; KeyFrame SHPs → SHPX when repacked):
  - `TEMPERAT.MIX` — temperate theater
  - `WINTER.MIX` — winter theater
  - `DESERT.MIX` — desert theater

Additional .MIX files may be loaded based on mission or expansion content, but the ones above are the minimum required for the core campaign.

## Preparing MIX files (Remix Web)

Use **[Remix Web](tools/remix-web/)** — a browser wizard that extracts MIX archives from your own GDI and NOD install discs, merges `GENERAL.MIX`, and repacks with [REMIX](tools/remix/) (11025 Hz audio, even byte offsets).

```bash
make remix-web          # from tiberiandawn/
# or: cd tools/remix-web && make
```

Open `tools/remix-web/web/dist/index.html` via a local static server, or run `npm run dev` in `tools/remix-web/web/` during development. You need both GDI and NOD install disc images (ISO or a ZIP containing one ISO each; volume labels must read `GDI` and `NOD`). Optionally attach the [itch.io release ZIP](https://indyjo.itch.io/commandconquer) to bundle `cnc.tos` and `*.W16` palette weights.

See [tools/remix-web/readme.md](tools/remix-web/readme.md) for full usage.