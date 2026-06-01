# Atari Port TODO

Tracked follow-ups for the Atari ST/MiNT port.

## Rendering / Present Path

- [ ] Re-enable screen shake on Atari ST (`Shake_The_Screen` in `tiberiandawn/conquer.cpp`).
  - Current status: disabled via early return under `#ifdef ATARI_ST`.
  - Root cause: offset `HidPage` → `SeenBuff` blits miss planar fast paths when `src_gb != dest_gb` (`Linear_Blit_To_Linear` in `tiberiandawn/atarilib/drawbuff.cpp` requires same buffer for `ST_Blitter_Planar_Rect_Blit`).
  - **Low-hanging fix:** add a cross-buffer planar branch (different `src_root` / `dst_root`, same dimensions) calling `ST_Blitter_Planar_Rect_Blit`; the blitter helper already supports distinct buffers.
  - Primary files: `tiberiandawn/conquer.cpp`, `tiberiandawn/atarilib/drawbuff.cpp`.

- [ ] Unify present paths: `GScreenClass::Render` → `Blit_Display`, menus/misc → `Blit_Hid_Page_To_Seen_Buff` (+ `ST_Screen_Apply_Game_Video_Hardware` on ST).
  - Goal: one backend-owned `render → present` contract (original WIN32 assumption), preserving mouse/UI composition order.
  - Primary files: `tiberiandawn/gscreen.cpp`, `tiberiandawn/conquer.cpp`, `tiberiandawn/atarilib/st_screen.cpp`.

- [x] ST 320×200 double-buffer strategy — **decided / in use**
  - Separate aligned planar visible + hidden pages in `startup.cpp`; `SeenBuff`/`HidPage` viewports; present via `Blit_Display` (in-game) or `Blit_Hid_Page_To_Seen_Buff` (menus, score, etc.).

## KeyFrame / Shape Decode Robustness

- [ ] Finish hardening XOR-chain `Build_Frame` when subframe table windows hit asset end.
  - Current status: endian-safe reload fix landed; some assets still log table-end cases.
  - Primary file: `tiberiandawn/keyframe.cpp`.
  - Desired outcome: avoid visual holes/artifacting from failed chain steps while preserving correctness.

## Startup / Platform Services

- [ ] Clean up stale `TODO` comments in `tiberiandawn/atarilib/startup.cpp` (TimerClass, `Set_Video_Mode`, audio) — behavior is implemented; comments mislead.
- [ ] Implement actual multi-window switching (`Change_Window` in `startup.cpp`) if ever needed on ST; current stub sets `Window` only.

## Content / Media Platform Gaps

- [ ] Port `Get_CD_Index` for Atari ST/MiNT (covert-CD detection).
  - **Not blocking default build:** `BONUS_MISSIONS`, `FRENCH`, and `GERMAN` are off in `tiberiandawn/defines.h`, so references in `init.cpp` / `conquer.cpp` are compiled out. No implementation is linked today.
  - Add a stub in `st_link_stubs.cpp` (return `-1`) if those defines are re-enabled before a real port.

## Tooling (host utilities)

Existing pipeline (see also `tiberiandawn/atari.md`):

| Tool | Role |
|------|------|
| [`remix`](tiberiandawn/tools/remix/readme.md) | Repack `.MIX` (even offsets, AUD99 → PCM) |
| [`paltool`](tiberiandawn/tools/paltool/paltool.c) | Extract 768-byte `.PAL` from BMP / CPS / WSA |
| [`palette-opt`](tiberiandawn/tools/palette-opt/readme.md) | Build single `C2P_WeightSet` (`.W16`) from one `.PAL` |
| `gen_cps_w16.py`, `histtool`, `w16fix` | Batch / histogram / fixups for asset prep |

- [ ] **`paltool`: partitioned / multi-palette support**
  - **Problem:** On PC, map selection (`mapsel.cpp`) drives several palette effects at once — grey earth fade (`GREYERTH.WSA` + `localpalette`), colourise (`E-BWTOCL.WSA` + `grey2palette`), spinning globe (`EARTH_*.WSA` + `Palette`), progress territories (`EUROPE.WSA` / `BOSNIA.WSA` / … + `progresspalette`), plus font pens (`_regpal`, `_greenpal`, `_othergreenpal`). Each WSA carries its own 768-byte VGA palette; the engine calls `Set_Palette` / `Read_Interpolation_Palette` per phase.
  - **ST constraint:** Only one 16-pen hardware palette and one active C2P weight set at a time (`C2P_Install_CustomWeights` / per-WSA `.W16`). Today map selection uses `WSA_DEFERRED_C2P_WEIGHTSET` and keeps a single weight set through the globe sequence because per-clip switches would mis-map composited pixels (see `#ifdef ATARI_ST` comment ~line 412 in `mapsel.cpp`).
  - **Goal:** Tooling (and eventually runtime) where **partition N** = one VGA palette + one ST 16-pen `subset[]` + one `weights[256][16]` table, so different index ranges (or logical sources) map through the correct VGA→ST mapping in one frame.
  - **`paltool` scope (first step):**
    - Accept multiple inputs (e.g. several `-i` WSA/CPS or `.PAL` files) with partition labels or index ranges.
    - Emit a structured description: per-partition 768-byte `.PAL`, optional merged manifest (JSON or header) listing VGA index subsets → partition id.
    - Optionally invoke / hand off to `palette-opt` per partition to produce one `.W16` each, or a single bundled multi-partition file once the on-disk format exists.
  - **Follow-on (not only paltool):** extend `C2P_WeightSet` or sidecar format if a single `.W16` must carry multiple partitions; runtime hook in WSA/C2P to select partition by palette index or blit context (mapsel progress animation + interactive map selection are the acceptance tests).
  - **Reference assets:** `GREYERTH.WSA`, `E-BWTOCL.WSA`, `EARTH_E.WSA` / `EARTH_A.WSA`, `EUROPE.WSA`, `MAP_LOCL.PAL`, `MAP_GRY2.PAL`, `MAP_PROG.PAL` interpolation tables.

## Low Priority Cleanup

- [ ] Review legacy `PG_TO_FIX` / old ST marker comments and convert still-relevant ones into explicit TODOs or remove obsolete ones.
  - Example files: `tiberiandawn/conquer.cpp`, `tiberiandawn/winstub.cpp`.

## Release documentation (player-facing)

Ship a dedicated Atari ST landing doc (suggested: `README-ATARI-ST.md` in the repo root, linked prominently from the main `README.md` and from release notes). Keep the existing upstream `README.md` as the general Vanilla Conquer entry; the ST page should link to it for license, community, and non-ST builds.

- [ ] **`README-ATARI-ST.md` landing page** (repo root; link from main [`README.md`](README.md) once written)
  - Port intro (Tiberian Dawn, 320×200 planar, STE DMA audio, WIP status).
  - Link to main [`README.md`](README.md) (license, community, non-ST builds).
  - Link to **[`atari-todo.md`](atari-todo.md)** (this list) for open work and release prep.
  - Link to [`tiberiandawn/atari.md`](tiberiandawn/atari.md) and [`tiberiandawn/atari-assets/readme.md`](tiberiandawn/atari-assets/readme.md).
  - **Host tools section:** short description + link per tool readme:
    - [`remix`](tiberiandawn/tools/remix/readme.md) — repack `.MIX` (alignment, AUD99 → PCM)
    - [`paltool`](tiberiandawn/tools/paltool/paltool.c) — extract `.PAL` from BMP/CPS/WSA *(add readme when convenient)*
    - [`palette-opt`](tiberiandawn/tools/palette-opt/readme.md) — build `.W16` C2P weight sets
    - [`histtool`](tiberiandawn/tools/histtool/readme.md) — color histograms for `palette-opt --hist`
    - [`w16fix`](tiberiandawn/tools/w16fix/w16fix.c) — reorder `.W16` subset pens *(document in palette-opt readme or own readme)*
    - [`list_mix.py`](tiberiandawn/list_mix/readme.md) — list MIX contents
    - [`gen_cps_w16.py`](tiberiandawn/tools/palette-opt/readme.md) — batch UI `.W16` generation
    - [`st_suite`](tiberiandawn/tests/st_suite/readme.md) — on-target test binary
  - Sections still to write on that page: screenshots, requirements/emulator, build, game-data prep, how to run (see checklist items below).

- [ ] **Screenshot(s)**
  - At least one in-game 320×200 capture (title or mission) for the readme and release assets.
  - Optional: before/after or menu shot; store under something like `docs/atari-st/` (not committed until we have rights-cleared captures).

- [ ] **System requirements & emulator settings**
  - **Hardware:** Atari ST with **BLiTTER** (build aborts without it); **STE-class** machine or emulator profile for digitized audio (same requirement as `st_suite` audio tests).
  - **Video:** Low resolution 320×200; game switches shifter to its own planar buffer (see `st_screen.cpp` / startup).
  - **OS:** TOS or MiNT; note any MiNT-specific paths (working directory, `CONQUER.INI`).
  - **Emulator (Hatari / Steem / etc.):** document a known-good profile — e.g. STE, BLiTTER on, 4 MB+ RAM, TOS 2.06 or EmuTOS if tested; how to set the game folder as cwd; keyboard/mouse/joystick mapping for menus.
  - **Not supported:** Remastered / Ultimate Collection data (align with main README policy).

- [ ] **How to build**
  - Toolchain: `m68k-atari-mint` cross GCC (default `$(HOME)/opt/cross-mint/bin` in `tiberiandawn/makefile`).
  - Commands: `cd tiberiandawn && make` (Release → `bin/AtariST/cnc.tos`; Debug → `cncd.tos`).
  - Optional: `make st-tests`, building host tools (`tools/remix`, `tools/palette-opt`) when preparing assets.
  - Point to `make help` / makefile `CONFIG=` / `CROSS_PREFIX=` overrides.

- [ ] **Game data: download & prepare**
  - **Obtain retail/freeware data** from [C&C Communications Center — The Game](https://cnc-comm.com/command-and-conquer/downloads/the-game) (GDI/NOD CD images or installer); user must own/hold a legitimate copy per EA terms.
  - **Install or extract** DOS/Gold layout so standard `.MIX` files exist (same set as PC TD; minimum list in `tiberiandawn/atari.md` § “Required MIX files”).
  - **Repack for ST:** run [`remix`](tiberiandawn/tools/remix/readme.md) on the game directory (`./remix -d /path/to/gamedata`) — even byte offsets for embedded payloads, AUD99 → 11 kHz PCM where needed.
  - **Ship conversion sidecars:** copy contents of [`tiberiandawn/atari-assets/`](tiberiandawn/atari-assets/readme.md) (`.W16` etc.) next to `cnc.tos`; document optional regeneration via `palette-opt` (see `atari.md`).
  - **Config:** `CONQUER.INI` beside the binary; note first-run / paths / `Refresh_Search_Drives` behavior on MiNT.

- [ ] **How to run**
  - Layout: `cnc.tos`, all required `.MIX` (post-`remix`), `CONQUER.INI`, and `atari-assets` files in **one directory** (cwd = executable directory).
  - Emulator: mount or copy that folder; launch `cnc.tos` from the desktop or shell.
  - Real hardware: same layout on floppy/hard disk partition; STE recommended for audio.
  - Troubleshooting appendix: no BLiTTER message, silent audio (non-STE), missing MIX, missing `.W16` warnings.

- [ ] **Release packaging checklist** (binary drop)
  - Pre-built `cnc.tos` + license/`COPYING` + `README-ATARI-ST.md` (no copyrighted MIX in the repo).
  - Optional: sample `CONQUER.INI` snippet; link to remix + asset docs only.

- [ ] **[itch.io](https://itch.io) store page** (public release landing)
  - Create project page (genre/tags: Atari ST, retro, strategy; platforms: Windows/macOS/Linux for emulator users, or “download + run in Hatari”).
  - Reuse/adapt copy from `README-ATARI-ST.md`: port pitch, requirements, build-your-own-data flow, link to [C&C Communications Center — The Game](https://cnc-comm.com/command-and-conquer/downloads/the-game).
  - **Upload:** `cnc.tos` zip (engine + `atari-assets` + readme only — no EA MIX files); optional separate “tools” zip or link to repo for `remix` sources.
  - **Media:** cover image + 315×250 thumbnail + 1–3 screenshots/GIF (same assets as repo `docs/atari-st/`).
  - **Legal blurb:** fan port / not affiliated with EA; user must supply own game data; GPL engine + third-party asset terms.
  - Link back to GitHub repo and `README-ATARI-ST.md`; cross-link itch URL from repo readme once live.

---

## Done (removed from active list)

- [x] **Timed palette fades** — `Fade_Palette_To` in `tiberiandawn/atarilib/palette.cpp` steps via `Bump_Palette` / `Wait_Vert_Blank`, not immediate set-to-target.
- [x] **`TimerClass` / 60 Hz timing** — `common/timer_st_vbl.cpp` + `St_Vbl_Timer_Init()` in `startup.cpp` after `Super(0)`.
- [x] **`Audio_Init` (STe DMA)** — `tiberiandawn/atarilib/audio_ste.cpp`, called from `startup.cpp`.
- [x] **`Set_Video_Mode` (LoRes game video)** — `ST_Screen_Enter_LoRes_Game_Video()` in `startup.cpp`.
- [x] **`remix` MIX repack tool** — `tiberiandawn/tools/remix/` (even-offset payloads, AUD99 → PCM where needed).
- [x] **WSA / XOR delta APIs** — `tiberiandawn/wsa.cpp` (not `atarilib/wsa.cpp`; header is `atarilib/wsa.h`).
