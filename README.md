# Command & Conquer — Atari ST port

Work-in-progress port of **Tiberian Dawn** to the Atari ST/STE: native `cnc.tos`, 320×200 planar graphics (BLiTTER), STE DMA audio.

![In-game screenshot (Nod base, 320×200)](docs/atari-st/screenshot-ingame.png)

**Status:** Game is rendered in 16 colors; playable in emulation; performance not great (usually single-digit FPS); FMV cutscenes supported.

- Upstream Vanilla Conquer (PC, other platforms): **[README-vanilla-conquer.md](README-vanilla-conquer.md)**
- Open tasks & release prep: **[atari-todo.md](atari-todo.md)**
- Port notes (MIX list, `.W16` format): **[tiberiandawn/atari.md](tiberiandawn/atari.md)**

## Requirements

- CPU: Acceptable framerates on Falcon+. FMV also works on 8MHz.
- RAM: 4MB supported (more RAM is better, especiall TT-RAM)
- Blitter is supported; if unavailable, software Blitting is used
- STe/Falcon DMA sound is supported; game remains silent on ST/TT.

## Run

See the [itch.io project page](https://indyjo.itch.io/cnc-atari-st).

## Build

`m68k-atari-mint` toolchain; then `cd tiberiandawn && make` → `bin/AtariST/cnc.tos`. On-target tests: `make st-tests` ([readme](tiberiandawn/tests/st_suite/readme.md)).

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

## Contributors

- **Jonas Eschenburg** — original Atari ST/STE port author.
- **Matthias Alles** — Falcon / accelerated-machine performance work.

## Legal

GPL engine code from [Vanilla Conquer](README-vanilla-conquer.md). Not affiliated with EA. You must provide your own game data; do not redistribute EA `.MIX` files with binaries.
