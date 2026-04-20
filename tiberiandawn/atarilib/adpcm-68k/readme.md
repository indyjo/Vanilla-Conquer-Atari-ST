# adpcm-68k integration notes

This directory contains an Atari ST integration derived from the ideas in
[Kalmalyzer/adpcm-68k](https://github.com/Kalmalyzer/adpcm-68k), adapted for
the Westwood AUD99 flavor used by Tiberian Dawn.

## What was imported/adapted

- The core 68000-oriented decode loop shape (decode packed byte into two nibble
  codes, update predictor/step index, emit PCM).
- Precomputed table approach for speed (`step_index x nibble -> delta`), but
  regenerated for **Westwood IMA** (`ADPCM_IMA_WS`, shift=3) rather than MS IMA.
- GNU-style inline assembly for nibble unpacking on m68k builds.

## Why this differs from upstream adpcm-68k

Upstream `adpcm-68k` targets classic IMA ADPCM streams. Westwood AUD99 uses a
different nibble expansion rule, so table generation and decode math here are
Westwood-specific.

## Upstream project

- Repository: [https://github.com/Kalmalyzer/adpcm-68k](https://github.com/Kalmalyzer/adpcm-68k)
- Original license is included verbatim in `LICENSE.md` in this directory.
