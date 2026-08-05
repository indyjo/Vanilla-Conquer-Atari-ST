# ST 4MB memory map — gist

Snapshot for recreating/updating the desert in-mission map. Full visual:
`st-4mb-memory-map.png` / `.svg`, interactive canvas under Cursor
`canvases/st-4mb-memory-map.canvas.tsx`.

## How it was built

1. Hatari dump `cncdump.bin` (gzip) → ST-RAM at **file offset `0x6ce9`**.
2. Match `tiberiandawn/bin/AtariST/cnc.tos` via basepage (`p_lowtpa` … `p_blen`).
3. Symbol ST address ≈ **`p_tbase + nm VMA`**.
4. Chase pointer globals (`SysMemPage`, heaps, `_ShapeBuffer`, MIX Data, AUDX/sprite/radar).
5. Layout is **physical addresses**, not alloc chronology. Gaps = free / unnamed.

Machine: 4 MiB · `_memtop` / screen = `0x3f8000` · desert theater, in-mission.

## Process image (from basepage)

| Region | Address | Size |
|--------|---------|------|
| Basepage | `0x2a4fe` | 256 |
| text | `0x2a5fe` | 1 230 040 |
| data | `0x156ad6` | 24 432 |
| bss | `0x15ca46` … `_end` `0x19f00e` | 271 816 |
| Stack | `0x3e8000` | 64 KiB (`__stksize`) |
| Visible planar | `0x3f8000` | 32 KiB (`SeenBuff`) |

## Below CNC TPA (`_membot` `0x108bc` → basepage)

| What | Address | Notes |
|------|---------|--------|
| Shell / AES | `0x108bc` | `_shell_p`, `PATH=` |
| ALERT (resident) | `0x12278` | `u:\pipe\alert` |
| XCONTROL (resident) | `0x17110` | `CONTROL.INF`, CPXs above |
| WWMouse / Palette / Tile·IconStage | `0x246d0`… | Early game ST-RAM |
| STE DMA | `0x2584c` | 1 KiB |
| ST shadow cache | `0x25c4c` | 2496 B |
| XCONTROL CPXs | `0x2660c`…`0x2a4fe` | `.CPX` names in dump |

## After BSS

| What | Address | Size |
|------|---------|------|
| **C heap (libcmini malloc)** | `0x19f00e` | ~64 KiB (`_end` → Overlays; mostly free) |
| FixedHeaps, SysMem, HidPage, MIX, caches… | see canvas `RAW[]` | via Buffer / Data pointers |

## Major in-TPA pointers (dump-verified)

| Symbol / role | Buffer |
|---------------|--------|
| SysMemPage | `0x1af62e` (64 000) |
| HidPage | `0x1bf100` (32 KiB) |
| Units … Houses heaps | `0x1c8be4` … (slot×count+2) |
| ShapeBuffer | `0x25d12e` (128 KiB) |
| LOCAL / CONQUER / SOUNDS.MIX | `0x27ee32` / `0x29255e` / `0x2d21de` |
| AUDX page slab | `0x2d712e` (212 992) |
| Map cells | `0x30d12e` (4096×58) |
| Sprite cache | `0x3483de` (167 248) |
| Radar arena | `0x37312e` (32 KiB) |
| DESERT.MIX | `0x37cd5e` (410 576) |

Contiguous free before stack ≈ **`0x3e112e`…`0x3e8000`** (~28 KiB).

## Refresh recipe

1. New Hatari dump while in desert mission; note `cnc.tos` build.
2. Confirm `RAM0` (search for basepage / `p_tbase` matching `nm`).
3. Re-read basepage + pointer globals; update `RAW` in the canvas (or this table).
4. Regenerate PNG: SVG from the same band list → `rsvg-convert -z 2`.

Do **not** trust HD `.MIX` byte-identity; use MFCD `Data` pointers + `DataSize`.
PH sizes in an old canvas may differ from the dump’s `p_tlen` — prefer the dump.
