# Atari ST on-machine test suite (`cnc_st_tests.tos`)

Separate from the main game binary: its own `main()`, links **`c2p` / `palette`** plus selected game objects (`Load_Title_Screen` from `ATARILIB/title_pcx_load.cpp`, `gbuffer`, `ccfile`, …) for the production-path menu test. Copy to a real Atari ST / MiNT system with a console for menu I/O.

Console output is wrapped to **40 columns** where longer messages are used.

## Build (cross-compile, same tree as `cnc.tos`)

```bash
cd TIBERIANDAWN
make st-tests
```

Output: `bin/AtariST/cnc_st_tests.tos`

## What it runs

| Option | Type | Purpose |
|--------|------|---------|
| **1** | Automated | **All automated tests:** C2P planar checksum + **Build_Frame** SHP spot-checks (CONQUER.MIX) + one **`.AUD`** playback + **ST16 convert** (in-place ICN, dither phase, padding) + **terrain left-clip ST16** blitter check. Single summary block at the end (`Overall: PASS` / `FAIL`). |
| **2** | Interactive | **16×16 VGA index grid** (8×8 px/cell) plus a **16-pen ST strip** (2 px below the grid) through C2P. Menu lists every **`*.W16`** in the **current directory**; after choosing one, select a **`*.PAL`** file from the cwd as well. The chosen **`.PAL`** supplies RGB, and the chosen **`.W16`** supplies weights/subset. Exits if no **`.W16`** or **`.PAL`** files are present. |
| **4** | Interactive | **Production title draw:** caches **`CONQUER.MIX`**, loads **`TITLE.CPS`** through **`Load_Title_Screen`** (same path as game), primes **`CurrentPalette`** with **TEMPERAT.PAL** before load (cold-start equivalent), then displays planar 320×200. This is the **title background only**, not the full main menu (buttons/text still need the real menu/dialog code). |
| **5** | Interactive | `TITLE.CPS` production path plus **main menu overlay** (dialog + gradient labels). |
| **6** | Interactive | `TITLE.CPS` plus **moving mouse cursor** using **`MOUSE.SHP`** from **`LOCAL.MIX`**. |
| **7** | Interactive | Console menu **`1`**–**`7`** (`E1`, `E2`, `POWER`, `MINIGUN`, `FIRE1`, `OPTIONS`, `TREX` from **`CONQUER.MIX`**); then **`Build_Frame` every frame** of the chosen SHP, tiled on a **4×4 checker** (logical **13**/**14**), grid **bottom-padded** (**8** px). **No** `printf` / `st_wrap_puts` during the low-rez preview; confirm with **silent** **Y**/**Z** vs **N** (Tips below). |
| **8** | Automated | Same as **1** (alias). Runs under **`Super(0L)`** with **`conterm`** key-click mute on MiNT. |
| **t** | Automated | **Terrain left-clip ST16** (also in **1**/**8**). Hatari: `tests/st_suite/run_tstcnc_hatari.sh t` (writes `TST_AUTO.CMD`, uses `--conout 2`). |
| **a** | Submenu | **Audio tests:** pick **1**–**n** for a fixed **MIX / .AUD** row, **n+1** or **f** for “first hit in list order” (same as **8**’s audio step), **m** for **dual-sample mix** (two selectable clips + volumes, VBL-serviced STE DMA — same path as theme + EVA), **0** / **ESC** back. The submenu stays in **supervisor mode** for its lifetime so **`conterm`** key-click muting is safe under MiNT memory protection. |
| **c** | Interactive | **CPS / W16 browser:** lists every **`.CPS`** referenced in game sources with its weight set (**`.W16`** on disk, or built-in **HTITLE** for **`TITLE.CPS`**). Pick an entry; image stays on screen until any key. |
| **g** | Interactive | **Iconset grid:** temperate **`.TEM` / `TRANS.ICN`** from **`TEMPERAT.MIX`**. **24×24** → ST16; other sizes → live C2P. Menu: number + **Enter**. **p** / **n** / **m**; needs **`TEMPERAT.W16`**. |

Input is read with **`Crawcin()`** (MiNT/TOS keyboard).

## Tips on real hardware

- For options **4**–**6**, place required MIX archives in the **current working directory**. These title tests use **`CONQUER.MIX` / `TITLE.CPS`** via the production `Load_Title_Screen` path (and **5**/**6** also open other MIX files).
- For **2**, copy any **`*.W16`** weight bundles and matching **`*.PAL`** files into the cwd. The test now asks for both: the **`.W16`** provides weights/subset, the **`.PAL`** provides RGB. (Host tools: `python3 tools/palette-opt/gen_cps_w16.py` or `palette-opt -o FILE.W16`, plus the source palette file.)
- For **g**, copy **`TEMPERAT.MIX`** and **`TEMPERAT.W16`** next to **`tstcnc.tos`**.
- For **c**, copy **`CONQUER.MIX`** plus companion **`.W16`** files from **`atari-assets/`** (host: `python3 tools/palette-opt/gen_cps_w16.py`). **`TITLE.CPS`** uses compiled **HTITLE** weights if **`TITLE.W16`** is absent.
- For **1** / **7** / **8**, add **`CONQUER.MIX`** next to the test `.TOS`. For **1** / **8** / **a** audio, copy the **MIX** files referenced by the built-in try list (e.g. **`SOUNDS.MIX`**, **`SCOUNDS.MIX`**, **`SCORES.MIX`** for **`AOI.AUD`** / **`IND2.AUD`**, **`TRANSIT.MIX`** for **`STRUGGLE.AUD`**, **`WIN1.AUD`**, side-select speech, etc.). With **9+** audio rows, use **`f`** for “first hit in list order” (single-digit **`9`** is the last clip). **Dual mix (`a` → `m`)**: place **`SCORES.MIX`** (theme) and **`SPEECH.MIX`** (EVA; `NEWOPT1.AUD`) for preset **Act on Instinct + New construction** (`p`). `SOUNDS.MIX` is SFX, not speech. API: `st_audio_asset_autotest.h`, `st_audio_mix_test.h`. (Cursor **`MOUSE.SHP`** is not a KeyFrame blob; menu **6** loads it from **`LOCAL.MIX`** via **`Extract_Shape`**.) Audio tests need **STE-class** digitized hardware (same as the main game).
- Interactive tests switch resolution and `Setscreen`. When they finish, they **restore the 16 ST hardware palette words** at `$FF8240` from before the test, then restore the previous `Getrez()` value.
- Interactive prompts accept **Y** or **Z** as “OK” (German QWERTZ). Test **7** uses a **silent** Y/N read during the video preview (no on-screen or serial prompt there); press **Y** or **Z** if the frame grid looks correct, **N** if not.
- If automated test **1** reports a checksum `FAIL` after you change `c2p.cpp` or the test pattern, update the golden constant `ST_C2P_AUTOTEST_PLANAR_CHECKSUM` in `st_c2p_autotests.cpp`.
- If you have no console when double-clicking under plain TOS, run from **MiNT** `bash` or a launcher that attaches a VT52 window.

## Adding tests

Add a `.cpp` under `tests/st_suite/`, declare `extern int st_run_...` from `st_tests_main.cpp`, append the object to `ST_TEST_OBJS` in the top-level `Makefile` inside the `ATARILIB` `st-tests` block, and extend this table.
