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

- [ ] **Remove `STE_AUD_FLAG_DUP2X` from runtime audio** (`tiberiandawn/atarilib/ste_aud_constants.h`, `ste_stream_pcm.cpp`, `audio_ste.cpp`, `Sample_Make_PCM`).
  - DUP2X was a producer-side playback hint (duplicate samples when DMA runs at ~25 kHz). Remix tooling will emit plain 11025 Hz 8-bit mono PCM with `rate=11025` and no DUP2X; sample-rate policy should be negotiated between asset prep and the STE driver, not via a flag in the `.AUD` header.
  - After remix lands: drop DUP2X handling in the pull path; play assets at the rate in the header.

- [ ] Port `Get_CD_Index` for Atari ST/MiNT (covert-CD detection).
  - **Not blocking default build:** `BONUS_MISSIONS`, `FRENCH`, and `GERMAN` are off in `tiberiandawn/defines.h`, so references in `init.cpp` / `conquer.cpp` are compiled out. No implementation is linked today.
  - Add a stub in `st_link_stubs.cpp` (return `-1`) if those defines are re-enabled before a real port.

## Tooling (host utilities)

Existing pipeline (see also `tiberiandawn/atari.md`):

| Tool | Role |
|------|------|
| [`remix`](tiberiandawn/tools/remix/readme.md) / `remix.tos` | Repack `.MIX` (even offsets, all audio → 11025 Hz 8-bit mono PCM) |
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

Upstream Vanilla Conquer readme: [`README-vanilla-conquer.md`](README-vanilla-conquer.md). Atari landing page: root [`README.md`](README.md).

- [x] **Root `README.md` (Atari port)** — landing page in place; links vanilla readme, this file, `atari.md`, tools, status line, screenshot.
  - Still to expand: Hatari profile details, troubleshooting, itch.io link, `COPYING` link, 4 MB hardware goal vs current Mega STE note.

- [x] **Screenshot(s)** — `docs/atari-st/screenshot-ingame.png` linked from root `README.md`.
  - Optional: title/menu shot for itch.io cover.

- [ ] **System requirements & emulator settings** *(partially in README: Mega STE / 10 MB / BLiTTER+DMA, WIP, 4 MB goal)*
  - [x] WIP framing + “not yet original ST” honesty in README.
  - [ ] **BLiTTER** called out explicitly (binary requirement at startup).
  - [ ] **Emulator recipe:** Hatari/Steem — STE, BLiTTER on, cwd = game folder, RAM; keyboard/mouse.
  - [ ] **OS:** TOS vs MiNT notes; working-directory behavior.
  - [x] Remastered / UC data not supported (README Run §1).

- [x] **How to build** *(basics in README)*
  - [ ] Add `CROSS_PREFIX` / `make help` to README if desired.

- [x] **Game data: download & prepare** *(basics in README Run)*
  - [x] cnc-comm download link; DOS not Remastered.
  - [x] Optional `remix` (not required).
  - [x] `atari-assets` sidecars beside `cnc.tos`.
  - [ ] Point readers to full MIX list in `atari.md` from README (one line).

- [ ] **How to run** *(partial: folder layout in README)*
  - [x] One-folder layout (`cnc.tos`, MIXes, `atari-assets`).
  - [ ] Emulator cwd / real-hardware notes; troubleshooting (BLiTTER, STE audio, missing `.W16`).

- [ ] **Gameplay / stability** *(called out in README status; track fixes here)*
  - [ ] Savegame **load** unreliable (save OK).
  - [ ] Performance: single-digit FPS typical; target 4 MB Mega STE (README goal).
  - [ ] Map select palette / progress animation (see **partitioned palettes** under Tooling).

- [ ] **Release packaging checklist** (binary drop)
  - Pre-built `cnc.tos` + license/`COPYING` + root `README.md` (no copyrighted MIX in the repo).
  - Optional: sample `CONQUER.INI` snippet; link to remix + asset docs only.

- [ ] **[itch.io](https://itch.io) store page** (public release landing)
  - Create project page (genre/tags: Atari ST, retro, strategy; platforms: Windows/macOS/Linux for emulator users, or “download + run in Hatari”).
  - Reuse/adapt copy from root `README.md`: port pitch, requirements, build-your-own-data flow, link to [C&C Communications Center — The Game](https://cnc-comm.com/command-and-conquer/downloads/the-game).
  - **Upload:** `cnc.tos` zip (engine + `atari-assets` + readme only — no EA MIX files); optional separate “tools” zip or link to repo for `remix` sources.
  - **Media:** cover image + 315×250 thumbnail + 1–3 screenshots/GIF (same assets as repo `docs/atari-st/`).
  - **Legal blurb:** fan port / not affiliated with EA; user must supply own game data; GPL engine + third-party asset terms.
  - Link back to GitHub repo and root `README.md`; cross-link itch URL from repo readme once live.

---

## Done (removed from active list)

- [x] **Timed palette fades** — `Fade_Palette_To` in `tiberiandawn/atarilib/palette.cpp` steps via `Bump_Palette` / `Wait_Vert_Blank`, not immediate set-to-target.
- [x] **`TimerClass` / 60 Hz timing** — `common/timer_st_vbl.cpp` + `St_Vbl_Timer_Init()` in `startup.cpp` after `Super(0)`.
- [x] **`Audio_Init` (STe DMA)** — `tiberiandawn/atarilib/audio_ste.cpp`, called from `startup.cpp`.
- [x] **`Set_Video_Mode` (LoRes game video)** — `ST_Screen_Enter_LoRes_Game_Video()` in `startup.cpp`.
- [x] **`remix` MIX repack tool (host)** — `tiberiandawn/tools/remix/` (even-offset payloads; expand to full audio normalize + MiNT `remix.tos`).
- [x] **WSA / XOR delta APIs** — `tiberiandawn/wsa.cpp` (not `atarilib/wsa.cpp`; header is `atarilib/wsa.h`).
