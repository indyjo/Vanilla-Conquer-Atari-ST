# remix specification

`remix` repacks Command & Conquer **plain TD-style MIX** archives for the Atari ST
port of Tiberian Dawn. It reads each embedded payload, optionally converts audio to
the STE DMA format, writes a new archive, and patches the MIX index once at the end.

This document captures behaviour and design decisions implemented in this tool.
For build and usage, see `readme.md`.

---

## Scope

### In scope

- Plain MIX files: 6-byte header + `count × 12`-byte index + data section.
- Per-payload type sniffing (AUD, PCX, SHP, ICN, ST16, …).
- Audio conversion to **11025 Hz, 8-bit mono, PCM** (compression type 0).
- **ST16 iconset conversion** in theater MIX files (optional, default on).
- **VQA → STVQ** conversion (`--convert-vqa`; CRC-named `video/` W16 sidecars).
- Even-byte payload alignment from the start of the MIX data section.
- Host CLI (`remix`) and browser WASM ([remix-web](../remix-web/)).

### Out of scope

- Encrypted MIX headers, checksum trailers, or RA-style extended headers.
- Compression types other than AUD **0**, **1**, and **99**.
- Non-integer resampling (e.g. 44100 → 11025).
- In-place rewrite without a temporary file.
- On-ST MiNT `remix.tos` (removed; use host remix or remix-web).

---

## MIX format (handled subset)

### File layout

| Offset | Size | Field |
|--------|------|--------|
| 0 | 2 | `count` (uint16 LE) — number of index entries |
| 2 | 4 | `data_size` (uint32 LE) — span of data section in bytes |
| 6 | 12 × count | Index entries |
| 6 + 12×count | data_size | Payload bytes |

Each **index entry** (12 bytes):

| Offset | Field |
|--------|--------|
| 0 | `crc` (uint32 LE) — CRC of uppercased filename |
| 4 | `offset` (uint32 LE) — byte offset from **start of data section** |
| 8 | `size` (uint32 LE) — payload size in bytes |

### Contiguity and padding

The format does **not** require payloads to be packed back-to-back. Each entry is
located only by its `(offset, size)` pair; bytes between `offset + size` of one
entry and the next entry’s `offset` are unused.

Original DOS MIX files are usually **contiguous with no gaps**. `remix` may insert
**1-byte gaps** for alignment (see below).

**Padding fill byte:** `0x00`.

### Index ordering on write

The output index is sorted by **CRC ascending as signed int32** (same order as
`MixFileClass::compfunc` / `bsearch` in the game). Payloads are written in
**source index order**; only the directory order changes.

### `data_size` on write

After all payloads are written, `data_size` is set to the final `body_pos` —
the total number of bytes in the data section **including** alignment padding,
i.e. the maximum of `new_offset + new_size` over all entries.

---

## I/O model

Processing is **not** in-place on the output stream:

1. Open input MIX read-only.
2. Create output file (host: user path; MiNT: `temp.mxx`).
3. Write a **placeholder** index (`data_size = 0`, offsets/sizes zeroed).
4. Append payloads sequentially, updating `new_offset` / `new_size` per entry.
5. **`fseek` to file start** and patch the final index (only MIX-level seek).

Per-entry AUD conversion writes the **final 12-byte AUD header** first (sizes
known from source metadata), then streams PCM forward. No seek-back to patch
individual AUD headers.

On success, MiNT renames `temp.mxx` → original `.mix`. On failure, `temp.mxx` is
removed.

---

## Payload alignment (Atari ST)

Before each payload, if `(data_start + body_pos)` is **odd**, one **`0x00`** byte
is written and `body_pos` is incremented.

- **Rationale:** STE DMA and some loaders prefer even-aligned buffers.
- **Note:** Original TD MIX files often use **odd** offsets; alignment is an ST
  port requirement, not a DOS MIX requirement.

---

## Type detection

Sniffing uses up to **512 bytes** (`REMIX_PROBE_LEN`) plus the full entry size
for consistency checks. Order:

1. AUD (strict; see below)
2. VOC (`Creative Voice File`)
3. VQA (`FORM`…)
4. PCX
5. SHP
6. PAL (768 or 772 bytes)
7. INI (heuristic)
8. MAP (8192 bytes)
9. `binary`

### AUD has no file magic

The 12-byte AUD header has **no signature**. Recognition is heuristic.

| Offset | Field |
|--------|--------|
| 0 | Sample rate (uint16 LE) |
| 2 | Compressed payload size (uint32 LE) |
| 6 | Uncompressed size (uint32 LE) |
| 10 | Flags: bit0 stereo, bit1 16-bit |
| 11 | Compression type |

**Supported compression types only:** `0` (PCM), `1` (Westwood), `99` (IMA99).
Any other value → not AUD (avoids false positives like `aud_3` on random binary).

### AUD acceptance rules (`remix_looks_like_aud`)

All must pass:

- `comp_size > 0`
- `comp_size == file_size - 12` (payload length must match header)
- `comp_size ≤ 16 MiB`, `uncomp` non-zero and `≤ 32 MiB`
- Type-specific checks below

**PCM (0):** `comp_size == uncomp`.

**Westwood (1):** header fields only (no frame magic).

**IMA99 (99):** first frame header at offset 12 must be valid when probe ≥
20 bytes:

- `comp` (u16) and `decomp` (u16) non-zero, `decomp` even
- Magic `0x0000DEAF` at frame offset +4
- `comp + 8 ≤ comp_size`

If probe &lt; 20 bytes, frame check is **skipped** (header-only path); full probe
is used during MIX processing.

### Type display string

Audio entries are labelled:

```text
<codec> <rate>/<bps>/<M|S>
```

Examples: `aud99 22050/16/M`, `aud_pcm 11025/8/M`.

After successful conversion: `aud_pcm 11025/8/M`.

---

## Audio conversion

### Target format

| Field | Value |
|-------|--------|
| Rate | 11025 Hz |
| Bits | 8 |
| Channels | mono |
| Compression | 0 (PCM) |
| Flags | 0 |
| `comp_size` / `uncomp` | equal (raw PCM byte count) |

Matches `audio_ste.cpp` STE DMA expectations. **No DUP2X flag** — sample rate is
real 11025 Hz, not 11025 doubled in hardware.

### When to convert (`remix_aud_needs_convert`)

Convert if payload passes `remix_looks_like_aud` and is **not** already target PCM.

**Skip conversion** (copy as-is) when:

- Already `aud_pcm 11025/8/M` (type 0, rate 11025, 8-bit mono).
- PCM with rate **below** 11025 (up-conversion not supported).

### Sample-rate normalization

Before resampling, rates in **(20000, 24000)** are treated as **22050 Hz**
(same quirk as `soundio_common.cpp` for values like 22222).

### Resampling

Only **integer decimation** to 11025 Hz:

- `factor = normalized_rate / 11025` must be exact.
- Output samples = `input_samples / factor`.
- Factor 2 fast path: average adjacent signed samples.
- Other factors: accumulate `factor` samples, emit mean.

Rates that cannot be reduced to 11025 with an integer factor → conversion fails.

### Codecs

| Source type | Decoder | Notes |
|-------------|---------|--------|
| 99 IMA99 | `remix_aud.c` | Frame-based; matches `ste_stream_ima99.cpp` |
| 1 Westwood | `remix_unzap.c` | Sliding-window delta; whole compressed block in memory |
| 0 PCM | inline | Stereo → mono average; 16-bit → `>> 8`; then resample |

### IMA99 details

- Framed payload: 8-byte frame header + compressed bytes per frame.
- Frame magic: `0x0000DEAF`.
- 16-bit flag in AUD header means **16-bit source domain** in `uncomp`; decode
  still emits **8-bit mono** (`predictor >> 8`), same as the game/ST stream code.
- Conversion pulls **one frame at a time** (`remix_ima_stream_pending_frame_samples`),
  decodes, writes via buffered block output.
- Total output samples from AUD `uncomp` and flags; trailing odd sample dropped
  (`total & ~1`).

### Output buffering

PCM is accumulated in a **4 KiB** buffer before `fwrite` to reduce syscall cost
on MiNT.

---

## Error handling

| Condition | Host (`remix`) | MiNT (`remix.tos`) |
|-----------|----------------|---------------------|
| Unsupported MIX header | Fail file | `SKIP … unsupported header`, continue |
| I/O failure | Fail file | Fail file |
| Convert failure | **Fail file** | **Fallback copy** of original payload |

Fallback copy: seek output back to payload start, rewrite original bytes unchanged.
Counts as `payload_errors` in stats.

WARN line format (ST):

```text
WARN <CRC8> <hint> copy
```

Hints: `decode` (decoder/size mismatch), `rate` (not resampleable), `size`
(header vs payload mismatch).

---

## User interface

### Host

- Banner: `input -> output (count files, data at offset)`.
- Table: CRC, old/new offset, old/new size, type (with ` -> ` if converted).

### MiNT (40-column)

- Banner + working directory.
- Per MIX: `=== name.mix ===`, then `CRC SIZE TYPE` per entry **before** work.
- Progress: `CONVERT` or `COPY`, 32 dots, verb flushed immediately; dots only
  when dot count increases.
- Summary block with right-aligned counts.
- `Press any key to continue…` at end.
- `temp.mxx` already exists → prompt Abort / Delete.

---

## Module map

| Module | Role |
|--------|------|
| `remix_mix.c` | MIX read/write, entry loop, alignment, stats |
| `remix_detect.c` | Type sniffing, AUD heuristics, fail hints |
| `remix_audio.c` | Convert orchestration, resample, output buffer |
| `remix_aud.c` | IMA99 streaming decode |
| `remix_unzap.c` | Westwood type-1 decompress |
| `remix_print.c` | Host table + ST 40-column UI |
| `remix_host.c` | CLI |
| `remix_st.c` | MiNT CWD batch, inplace rename |

Shared headers: `remix.h`, `remix_aud.h`, `remix_detect.h`, `remix_audio.h`,
`remix_print.h`, `remix_unzap.h`.

---

## Constants (reference)

| Symbol | Value | Meaning |
|--------|-------|---------|
| `REMIX_TARGET_RATE` | 11025 | Output sample rate |
| `REMIX_PROBE_LEN` | 512 | Type sniff bytes |
| `REMIX_COPY_CHUNK` | 16384 | Stream copy buffer |
| `REMIX_OUT_BUF` | 4096 | PCM write buffer |
| `REMIX_TEMP_MXX` | `temp.mxx` | MiNT temp archive |
| `REMIX_LINE_WIDTH` | 40 | ST line width |
| `REMIX_PROGRESS_DOTS` | 32 | Progress bar width |

---

## Known limitations

- Corrupt IMA99 frame chains (valid frame 0, broken frame 1+) cannot be
  converted; ST falls back to copy.
- MIX archives with duplicate CRCs or overlapping offsets are not validated.
- `aud.mix` and other pre-remixed archives with target PCM are copied unchanged.
- Detection can still mis-label edge cases; strict `comp_size == payload` and
  IMA `DEAF` check greatly reduce false AUD positives.
- PCM path still writes sample-at-a-time through the resampler helper (not
  frame-blocked like IMA).

---

## MIX merge (dual-disc GENERAL.MIX)

When both GDI and NOD install discs are available, `GENERAL.MIX` from each side must be
combined before repack. `remix_mix_merge()` unions index entries from multiple plain MIX
inputs:

| Case | Action |
|------|--------|
| CRC not yet in output | add entry (payload copied from that source file) |
| Same CRC, same size | skip (keep first) |
| Same CRC, different size | error |

Output index is sorted by **CRC ascending as signed int32** (same as repack). Merge does
not convert audio or apply even-byte alignment — run `remix_mix_file()` (or
`remix_mix_merge_and_repack()`) afterward.

Host CLI:

```bash
remix -o general.mix gdi/GENERAL.MIX nod/GENERAL.MIX   # merge + repack
remix -o output.mix input.mix                            # repack only
```

---

## WebAssembly UI mode

`RemixConfig.ui` selects output behaviour:

| Mode | Use |
|------|-----|
| `REMIX_UI_HOST` | Host CLI table (`remix_print_host_*`) |
| `REMIX_UI_ST` | 40-column MiNT UI |
| `REMIX_UI_WASM` | Silent — no stdout; optional `entry_report` callback |

`remix_mix_file_ex()` is the full repack entry point. Set `entry_report` to receive
each `RemixEntry` after processing (CRC, sizes, `type_in` / `type_out`). Used by
[remix-web](../remix-web/) WASM glue (`remix_wasm.c`).

---

## ST16 iconset conversion (theater MIX files)

When `RemixConfig.convert_st16_iconsets` is enabled (default on host CLI and
`remix.tos`), remix converts standard 8bpp iconsets in these MIX basenames:

| MIX | W16 stem | Icon extensions |
|-----|----------|-----------------|
| `TEMPERAT.MIX` | `TEMPERAT` | `.TEM` |
| `DESERT.MIX` | `DESERT` | `.DES` |
| `WINTER.MIX`, `SNOW.MIX` | `WINTER` | `.WIN` |
| `JUNGLE.MIX` | `JUNGLE` | `.JUN` |

Before processing entries, remix loads `<stem>.W16` from `RemixConfig.w16_dir`
(NULL = current working directory) and installs C2P weights via the same path as
the game (`ST16_Convert_InPlace` in `atarilib/st16_convert.cpp`).

### Detection (`type_in`)

| Type | Condition |
|------|-----------|
| `st16` | Native ST16 blob (chunk at `0x20`, `Icons ≥ 0x2c`, BE header) |
| `icn` | Standard convertible iconset (`Icons = 0x20`, valid IControl tail) |
| (other) | Existing heuristics unchanged |

Non-iconset payloads in theater MIX files are copied unchanged.

### Failure policy

Unlike audio, ST16 conversion **does not** fall back to copying the original
payload. If the required `.W16` is missing or conversion fails, the entire MIX
repack fails.

### Host CLI flags

| Flag | Meaning |
|------|---------|
| (default) | ST16 conversion enabled |
| `--no-st16-iconsets` | Skip ST16 conversion |
| `--w16-dir PATH` | Directory containing `TEMPERAT.W16`, etc. |

### Stats

`RemixStats` adds: `iconset_files`, `iconset_converted`, `iconset_already_st16`,
`iconset_errors`.

---

## AUDX (external-pool audio)

Eligible MIXes: `SOUNDS.MIX`, `SPEECH.MIX`, `SCORES.MIX`. After AUD→PCM, `--audx` wraps each PCM AUD as a 28-byte BE **AUDX** meta record and appends sample bytes to `pool%04x.bin` (pool ids 5 / 6 / 7). See `tiberiandawn/atari.md` for the on-disk layout and runtime page-cache rules.

`remix-web` (0.3.x + Audio) additionally moves classic AUD entries out of `TRANSIT.MIX` into `SOUNDS.MIX` before rempack, so AUDX metas land in the cached SFX MIX. `TRANSIT.MIX` cannot be MFCD-cached (WSA/VQA/RECORD).

`SCORES.MIX` omits CRC `0x5CE4DFD8` (`AOI.VAR`) from the output directory (no body, no pool bytes). Runtime already falls back to `AOI.AUD` when the variation file is missing.

### Host CLI flags

| Flag | Meaning |
|------|---------|
| `--audx` | Convert PCM AUD → AUDX + pool sidecar |
| `--audx-pool-id ID` | Override pool id (default from MIX basename) |

`RemixStats` adds: `audx_files`, `audx_converted`, `audx_errors`.

---

## References

- Westwood AUD: [ModdingWiki](https://moddingwiki.shikadi.net/wiki/Westwood_AUD_Format), [aud3.txt](http://vladan.bato.net/cnc/aud3.txt) (document revision, not codec type 3).
- Game code: `common/mixfile.h`, `common/soundio_common.cpp`, `tiberiandawn/atarilib/audio_ste.cpp`, `ste_stream_ima99.cpp`.
- MIX authoring: `tools/mixtool/mixcreate.h`.
