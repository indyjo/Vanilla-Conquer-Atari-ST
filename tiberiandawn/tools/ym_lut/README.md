# YM digi LUT generator

Generate YM2149 digi look-up tables as movep-ready C/asm includes,
using Hatari’s three amplitude models.

## Usage

```bash
cd tiberiandawn/tools/ym_lut
# Digi_Submit path: 64 uniform bins (andi #$FC), linear scale, no remap
python3 ym_lut_gen.py --mode grid --model table --channels 2 --bits 6 --asm --out-dir out
# Legacy: thin + remap
python3 ym_lut_gen.py --mode thin --model table --channels 2 --bits 6 --out-dir out
```

Stdlib only (no third-party packages).

| Flag | Values | Meaning |
|------|--------|---------|
| `--mode` | `grid` `thin` | Uniform linear bins (default) / legacy thin+remap |
| `--model` | `table` `math` `linear` | Hatari ST measured / circuit / linear mean |
| `--channels` | `1` `2` `3` | Digi voices (others held at volume 0) |
| `--bits` | `8` `7` `6` `5` | LUT size 256 / 128 / 64 / 32 |
| `--pcm` | `signed` `unsigned` | Sample polarity (grid targets + thin remap) |
| `--asm` | flag | Also emit `*_lut.S.inc` for `audio_timer_dac_ym.S` |
| `--out-dir` | path | Output directory |
| `--prefix` | name | Symbol prefix |

## Grid mode (Digi HAL)

64 slots, indexed by `sample & 0xFC` (byte offset into 64×4 longs)—no remap.

With `--pcm signed` (default), each slot targets amplitude from `s ^ 0x80`, so
signed silence (`s=0`) is **mid** amplitude and the table is bipolar in index
order (mid → loud → quiet → mid). With `--pcm unsigned`, slot 0 is quiet and
the ramp is monotone quiet→loud.

## Thin mode (legacy)

Identity-optimal 256-ramp, greedy least-RMS subset, plus `remap[256]` as byte offsets.

## Movep layout

| Ch | Size | Bytes |
|----|------|-------|
| 1 | 2 | `08 va` |
| 2 | 4 | `08 va 09 vb` |
| 3 | 8 | `08 va 09 vb 0a vc 00 00` |
