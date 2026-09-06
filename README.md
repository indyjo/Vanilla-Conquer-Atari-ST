# Command & Conquer — Atari ST port

Work-in-progress port of **Tiberian Dawn** to the Atari ST/STE: native `cnc.tos`, 320×200 planar graphics (BLiTTER), STE DMA audio, YM-2149 digi on plain ST.

![In-game screenshot (Nod base, 320×200)](docs/atari-st/screenshot-ingame.png)

**Status:** Game is rendered in 16 colors; playable in emulation. 8 MHz ST/STe is still a few frames per second (about 2–4 with digi, 4–5 with audio mostly off); Falcon is around 11–12 fps, TT around 30. FMV cutscenes are supported. 

- Performance measurements in **[Benchmark.md](Benchmark.md)**.
- Upstream Vanilla Conquer (PC, other platforms): **[README-vanilla-conquer.md](README-vanilla-conquer.md)**
- Open tasks & release prep: **[atari-todo.md](atari-todo.md)**
- Port notes (MIX list, `.W16` format): **[tiberiandawn/atari.md](tiberiandawn/atari.md)**

## Requirements

- CPU: Acceptable framerates on Falcon+. FMV also works on 8 MHz.
- RAM: 4 MB supported (more RAM is better, especially TT-RAM)
- Blitter is supported; if unavailable or slower than CPU, software blitting is used
- Digitized audio: `[AtariST] Audio=Auto` uses STE DMA when present, otherwise YM-2149 (~6.25 kHz). Force with `Audio=STE` / `Audio=YM` / `Audio=Covox` / `Audio=None` (see `tiberiandawn/atari.md`).
- On ≤16 MHz-class machines, a boot CPU probe (autotune) turns on idle-animation throttles and freezes AI during map gestures. Override in `CONQUER.INI` `[AtariST]` (see [Benchmark.md](Benchmark.md) / `tiberiandawn/atari.md`).

## Run

See the [itch.io project page](https://indyjo.itch.io/cnc-atari-st), or [Benchmark.md](Benchmark.md) for measuring performance.

## Build

`m68k-atari-mint` toolchain plus **libcmini**. If libcmini is not already in the MiNT sys-root, point Make at its install prefix (the path must end in `/usr`):

```sh
cd tiberiandawn
make LIBCMINI_PREFIX="$HOME/opt/libcmini/usr" -j4
```

Output: `bin/AtariST/cnc.tos` (link-time optimization is on by default). Host tests: `make tests` ([tests/README](tiberiandawn/tests/README.md)). On-target tests: `make st-tests` ([st_suite readme](tiberiandawn/tests/st_suite/readme.md)).

## Host tools

| Tool | Description |
|------|-------------|
| [remix](tiberiandawn/tools/remix/readme.md) / [remix-web](tiberiandawn/tools/remix-web/) | Repack `.MIX` (even offsets, audio → 11025 Hz PCM / AUDX, optional ST16/SHPX/STVQ) |
| [vqatool](tiberiandawn/tools/vqatool/readme.md) | Inspect Westwood VQA; encode FORM `STVQ` (`.stv`) for Atari |
| [stvqview](tiberiandawn/tools/stvqview/readme.md) | On-target `.stv` player (`stvqview.ttp`) for encode checks |
| [paltool](tiberiandawn/tools/paltool/paltool.c) | Extract 768-byte `.PAL` from BMP / CPS / WSA |
| [palette-opt](tiberiandawn/tools/palette-opt/readme.md) | Build `.W16` chunky→planar weight sets |
| [histtool](tiberiandawn/tools/histtool/readme.md) | Frame histograms for `palette-opt --hist` |
| [w16fix](tiberiandawn/tools/w16fix/) | Reorder `.W16` hardware pen subset (identity + greedy remap) |
| [w16sort](tiberiandawn/tools/w16sort/) | Reorder `.W16` pens by YUV Hamiltonian path (needs `.PAL`) |
| [wsa_palette_report.py](tiberiandawn/tools/wsa_palette_report.py) | Dump WSA header/palette info; compare against a reference `.PAL` |
| [list_mix](tiberiandawn/list_mix/readme.md) | List MIX contents (CRC, names) |
| [ym_lut](tiberiandawn/tools/ym_lut/README.md) | Generate YM-2149 digi LUTs (`audio_timer_dac_ym.S`) |

## Contributors

- **Jonas Eschenburg** — original Atari ST/STE port author.
- **Matthias Alles** — Falcon / accelerated-machine performance work.

## Legal

GPL engine code from [Vanilla Conquer](README-vanilla-conquer.md). Not affiliated with EA. You must provide your own game data; do not redistribute EA `.MIX` files with binaries.
