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
| **1** | Automated | Builds a synthetic 320×200 chunky buffer, runs `C2P_Render_Logical_To_ST_Screen`, samples planar corners, checks a **golden** planar checksum (prints `OK` / `FAIL` with expected value). |
| **2** | Interactive | Switches to low rez, shows one horizontal black→white sweep via C2P; **Y/N** whether it looks correct (catches “half duplicated” style bugs). |
| **3** | Interactive | Extracts **`HTITLE.PCX`** from **`UPDATE.MIX`**, decodes PCX, **nearest-neighbour scales to 320×200** if needed (e.g. 640×400), then **Y/N** (or **Z** for OK on German QWERTZ). |
| **4** | Interactive | **Production HTITLE draw:** writes **`ST_HTEST.PCX`** (from `UPDATE.MIX`), primes **`CurrentPalette`** like startup, then **`Load_Title_Screen`** (same **C2P / `Scale`** as the game) into planar 320×200. This is the **title background only**, not the full main menu (buttons/text still need the real menu/dialog code). Compare with **3**. |
| **5** | Automated | Same as **1**, then prints PASS/FAIL. |

Input is read with **`Crawcin()`** (MiNT/TOS keyboard).

## Tips on real hardware

- For options **3** and **4**, place **`UPDATE.MIX`** in the **current working directory**. **4** briefly creates **`ST_HTEST.PCX`** (then deletes it) so **`CCFileClass`** can open the same asset path style as loose files on disk.
- Interactive tests switch resolution and `Setscreen`. When they finish, they **restore the 16 ST hardware palette words** at `$FF8240` from before the test, then restore the previous `Getrez()` value.
- Interactive prompts accept **Y** or **Z** as “OK” (German QWERTZ).
- If automated test **1** reports a checksum `FAIL` after you change `c2p.cpp` or the test pattern, update the golden constant `ST_C2P_AUTOTEST_PLANAR_CHECKSUM` in `st_c2p_autotests.cpp`.
- If you have no console when double-clicking under plain TOS, run from **MiNT** `bash` or a launcher that attaches a VT52 window.

## Adding tests

Add a `.cpp` under `tests/st_suite/`, declare `extern int st_run_...` from `st_tests_main.cpp`, append the object to `ST_TEST_OBJS` in the top-level `Makefile` inside the `ATARILIB` `st-tests` block, and extend this table.
