# stvqview

Standalone Atari ST/STE player for FORM `STVQ` (`.stv`) clips produced by
[`vqatool`](../vqatool/). See [`../vqatool/stvq.md`](../vqatool/stvq.md) for the
bitstream.

## Build

```sh
cd tiberiandawn/tools/stvqview
make
```

Uses `$(HOME)/opt/cross-mint/bin/m68k-atari-mint-gcc` by default (same as the
game). Override with `CROSS_PREFIX=` / `CROSS_TARGET=`.

## Run

```text
stvqview.ttp file.stv
```

| Key | Action |
|-----|--------|
| ESC | Quit |
| Space | Pause / resume |

## Behaviour (v1)

- ST LoRes 320×200; clip centered (X snapped to 8 px)
- Real ping-pong: two phys screens; `STVD` skip bits leave the back buffer (frame N−2)
- `STPL` applied on the VBL that reveals that frame
- STE DMA audio @ 12.517 kHz is the clock when sound + DMA are available
- STFM / no-DMA: silent video, paced by VBL ≈ `50/fps`
- Streams from disk; `STFI` ignored (no seek yet)

## Profiling

On exit (ESC or end of clip), prints a `_hz_200` (200 Hz) timing report and writes
`STVQPROF.TXT` in the current directory:

| Bucket | Meaning |
|--------|---------|
| `read` | One `fread` of the full `STFR` payload |
| `stcr` | Codebook replaces from memory |
| `decode` | `STVD` → `movep` into back buffer |
| `audio` | DMA ring submit (`memcpy` from `frame_buf` SND0) |
| `present` | Whole `present()` call |
| `vbl` | Spin waiting for the reveal VBL (`present_done`) |
| `wait` | Audio DMA drain or silent VBL pace |

Startup also prints `dma_ok` / whether STE-DMA clocking is active.
