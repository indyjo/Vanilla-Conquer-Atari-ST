# STVQ — Atari ST video format

IFF container for 16-color ST LoRes playback (8×8 `movep` tiles, STE DMA audio).
Not compatible with Westwood VQA payloads; only IFF framing is shared.

All multi-byte integer fields are **big-endian**.
Chunk payloads may be odd-sized; on disk they are padded to an even length (`(size+1)&~1`). The pad is not included in `size`.

## File

```
FORM 'STVQ'
  STHD
  NAME?                 optional ASCII
  STPL                  initial palette
  STCB?                 unused (legacy; ignore if present)
  STFI?                 optional frame index
  STFR…                 one chunk per frame
  STEN                  end (size 0)
```

Playback loop: read `STFR` payload (IFF-padded), and the next 8 bytes (following chunk ID+size) in one I/O. Stop when lookahead is `STEN`.

## Chunks

| ID | Role |
|----|------|
| `STHD` | Header |
| `NAME` | Optional encoder/string |
| `STPL` | 16 STE colors (32 bytes) |
| `STCB` | **Unused** (legacy full codebook; optional) |
| `STCR` | Sparse codebook replaces (inside `STFR`) |
| `STFI` | Frame offsets |
| `STFR` | One presentation frame |
| `STVD` | Video (inside `STFR`) |
| `SND0` | PCM audio (inside `STFR`) |
| `STEN` | End marker, `size=0` |

Reuse of Westwood IDs only where payload matches (`FORM`, `NAME`, `SND0` = raw PCM). Everything else uses `ST*` IDs.

## `STHD`

32-byte big-endian header:

| Offset | Type | Field |
|--------|------|--------|
| 0 | u16 | `version` (1) |
| 2 | u16 | `flags` (bit0 = has sound) |
| 4 | u16 | `frames` |
| 6 | u16 | `width` |
| 8 | u16 | `height` |
| 10 | u8 | `block_w` (8) |
| 11 | u8 | `block_h` (8) |
| 12 | u8 | `fps` |
| 13 | u8 | reserved |
| 14 | u16 | `cb_entries` |
| 16 | u16 | `sample_rate` (12517) |
| 18 | u8 | `channels` (1) |
| 19 | u8 | `bits_per_sample` (8) |
| 20 | u16 | `max_frame_bytes` hint |
| 22 | u16 | reserved |
| 24 | u32 | reserved |

Tile grid: `tiles_x = ceil(width/8)`, `tiles_y = ceil(height/8)`. Encoded coverage may extend past the visible edge; pad samples clamp to the nearest covered pixel (edge extend). Player crops to `width`×`height`.

`cb_entries` sizes the in-memory codebook. The encoder does not emit an initial dictionary; the player allocates a zeroed buffer of `cb_entries × 32` bytes and fills it via `STCR`.

## `STPL`

16 × `uint16` STE color registers (32 bytes). May also appear inside an `STFR` for mid-stream changes. Independent of codebook updates.

## `STCB` (unused)

Legacy full codebook: `cb_entries` × 32-byte tiles (ST interleaved planar 4bpp).

**Current encoders omit `STCB`.** Players may accept it for old files (load into the codebook buffer) or ignore unknown/optional header chunks; if absent, start from a zeroed codebook of `cb_entries` tiles. New encoders must not rely on `STCB`.

## `STCR`

Inside `STFR`, before `STVD`. Payload:

```
{ uint16 cb_index; uint8 tile[32]; } × (size/34)
```

`size` must be a multiple of 34. No protocol cap on count; encoder/runtime budgets are operational.

Apply replaces before drawing that frame’s `STVD`. This is the sole way new encoders populate the codebook.

## `STFI`

Optional. `frames` × `uint32` file offsets to each `STFR` chunk header.

## `STFR`

One frame. Nested chunks, typical order:

```
STFR
  STPL?                 optional palette change
  STCR?                 optional codebook replaces
  STVD                  video (required)
  SND0?                 audio
```

`STPL` / `STCR` / skip policy around cuts are encoder concerns; the format does not require full redraw or codebook rebuild after `STPL`.

## `STVD`

Column-major, left → right:

```
for col in 0 .. tiles_x-1:
  uint32 skip_mask
  uint16 index[k]       // k = number of non-skip tiles in this column
```

**Skip mask:** bit 31 = top tile row (row 0), bit 30 = row 1, … downward.
Unused low bits are 0 (`tiles_y` ≤ 32). Decode walks MSB→LSB with
`add.l mask,mask` / branch on extend (or carry): set = skip (unchanged vs
frame **N−2**), clear = draw next `uint16` codebook index.

Example (`tiles_y = 20`, skip rows 0,1,18,19): `mask = 0xC0000003`.

Frames 0–1 are full draws (encoder). Later full draws are encoder policy.

**Index stream:** only for clear bits, top → bottom within the column. Each `uint16` is a codebook entry index.

**Alignment:** `size` is always even (`4*tiles_x + 2*indices`). Nested IFF padding keeps the payload word-aligned, so the player may load mask/indices with native big-endian `u32`/`u16` reads.

## `SND0`

Raw signed 8-bit mono PCM. `size` is the sample count and **must be even**
(word-aligned payload; encoder rounds odd frame lengths up by one sample).
Length may still vary per frame. Even `SND0` also keeps subsequent IFF chunks word-aligned.

**Audio is the reference clock.** Video may jitter vs 50 Hz VBL; short lead/lag vs audio is normal. Do not require a fixed sample count per frame.

## `STEN`

`size = 0`. End of frame list.

## Player outline

1. Read `STHD` and initial `STPL`. Allocate zeroed codebook of `cb_entries` tiles. If a legacy `STCB` is present, load it instead.
2. Loop: read `STFR` + 8-byte lookahead.
3. Optional `STPL` → hardware palette; optional `STCR` → patch codebook.
4. Decode `STVD` (column mask → `movep` or skip).
5. Queue `SND0` to STE DMA; pace video from audio + VBL.
6. Until lookahead `STEN`.
