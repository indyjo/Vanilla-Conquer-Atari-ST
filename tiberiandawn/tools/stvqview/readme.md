# stvqview

Standalone Atari ST/STE player for FORM `STVQ` (`.stv`) clips produced by
[`vqatool`](../vqatool/). See [`../vqatool/stvq.md`](../vqatool/stvq.md) for the
bitstream.

The decode/hw runtime lives in [`../../atarilib/stvq/`](../../atarilib/stvq/)
(shared with the game `Play_Movie` path). This directory is a thin CLI
(`stvqview.c` + FILE* `StvqIo` adapter).

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

- ST LoRes 320x200; clip centered (X snapped to 8 px)
- Real ping-pong: two phys screens; `STVD` skip bits leave the back buffer (frame N-2)
- `STPL` applied on the VBL that reveals that frame
- STE DMA audio @ 12.517 kHz is the clock when sound + DMA are available
- STFM / no-DMA: silent video, paced by VBL ~= `50/fps`
- Streams from disk; `STFI` ignored (no seek yet)

Startup also prints `dma_ok` / whether STE-DMA clocking is active.
