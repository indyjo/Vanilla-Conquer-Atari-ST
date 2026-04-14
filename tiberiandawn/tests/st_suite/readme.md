# Atari ST on-machine test suite (`cnc_st_tests.tos`)

Separate from the main game binary: its own `main()`, links **`c2p` / `palette`** plus selected game objects (`Load_Title_Screen` / `Read_PCX_File` from `ATARILIB/title_pcx_load.cpp`, `gbuffer`, `ccfile`, …) for the production-path menu test. Copy to a real Atari ST / MiNT system with a console for menu I/O.

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
| **1** | Automated | Builds a synthetic 320×200 chunky buffer with **TEMPERAT.PAL** (`Set_Palette`), runs `C2P_Render_Logical_To_ST_Screen`, samples planar corners, checks a **golden** planar checksum (prints `OK` / `FAIL` with expected value). |
| **2** | Interactive | Switches to low rez, **TEMPERAT.PAL** logical + hardware first-16 pens (same as game startup), one horizontal luminance sweep via C2P; **Y/N** whether it looks correct (catches “half duplicated” style bugs). |
| **3** | Interactive | Extracts **`HTITLE.PCX`** from **`UPDATE.MIX`**, decodes PCX, **nearest-neighbour scales to 320×200** if needed (e.g. 640×400), then **Y/N** (or **Z** for OK on German QWERTZ). |
| **4** | Interactive | **Production HTITLE draw:** writes **`ST_HTEST.PCX`** (from `UPDATE.MIX`), primes **`CurrentPalette`** with **TEMPERAT.PAL** before load (like a real cold start), then **`Load_Title_Screen`** (same **C2P / `Scale`** as the game) into planar 320×200. Hardware pens follow the **HTITLE** PCX palette. This is the **title background only**, not the full main menu (buttons/text still need the real menu/dialog code). Compare with **3**. |
| **5** | Interactive | HTITLE production path plus **main menu overlay** (dialog + gradient labels). |
| **6** | Interactive | HTITLE plus **moving mouse cursor** using **`MOUSE.SHP`** from **`CCLOCAL.MIX`**. |
| **7** | Interactive | Console menu **`1`**–**`7`** (`E1`, `E2`, `POWER`, `MINIGUN`, `FIRE1`, `OPTIONS`, `TREX` from **`CONQUER.MIX`**); then **`Build_Frame` every frame** of the chosen SHP, tiled on a **4×4 checker** (logical **13**/**14**), grid **bottom-padded** (**8** px). **No** `printf` / `st_wrap_puts` during the low-rez preview; confirm with **silent** **Y**/**Z** vs **N** (Tips below). |
| **8** | Automated | Runs **1** (C2P checksum), then a fixed **`Build_Frame`** multi-SHP list in `st_build_frame_assets.cpp` (includes **E1**, **OPTIONS**, **TREX**, etc.; skips missing files). No screen prompts. |

Input is read with **`Crawcin()`** (MiNT/TOS keyboard).

## Tips on real hardware

- For options **3**–**6**, place **`UPDATE.MIX`** (and for **5**/**6** the other MIX files those tests open) in the **current working directory**. **4** briefly creates **`ST_HTEST.PCX`** (then deletes it) so **`CCFileClass`** can open the same asset path style as loose files on disk.
- For **7** / **8**, add **`CONQUER.MIX`** next to the test `.TOS`. (Cursor **`MOUSE.SHP`** is not a KeyFrame blob; menu **6** still loads it from **`CCLOCAL.MIX`** via **`Extract_Shape`**.)
- Interactive tests switch resolution and `Setscreen`. When they finish, they **restore the 16 ST hardware palette words** at `$FF8240` from before the test, then restore the previous `Getrez()` value.
- Interactive prompts accept **Y** or **Z** as “OK” (German QWERTZ). Test **7** uses a **silent** Y/N read during the video preview (no on-screen or serial prompt there); press **Y** or **Z** if the frame grid looks correct, **N** if not.
- If automated test **1** reports a checksum `FAIL` after you change `c2p.cpp` or the test pattern, update the golden constant `ST_C2P_AUTOTEST_PLANAR_CHECKSUM` in `st_c2p_autotests.cpp`.
- If you have no console when double-clicking under plain TOS, run from **MiNT** `bash` or a launcher that attaches a VT52 window.

## Adding tests

Add a `.cpp` under `tests/st_suite/`, declare `extern int st_run_...` from `st_tests_main.cpp`, append the object to `ST_TEST_OBJS` in the top-level `Makefile` inside the `ATARILIB` `st-tests` block, and extend this table.
