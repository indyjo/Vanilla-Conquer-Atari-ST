# Tiberian Dawn tests

## Layout

| Directory | Platform | Purpose |
|-----------|----------|---------|
| [`host/`](host/) | Host (macOS / Linux) | Fast unit / identity tests; no Atari toolchain |
| [`st_suite/`](st_suite/) | Atari ST (`.TOS`) | On-machine / Hatari suite (`tstcnc.tos`) |

Repo-root [`tests/`](../../tests/) is the separate Vanilla Conquer CMake suite (common/graphics); it is unrelated to this makefile.

## Naming

**Host**

- Sources: `test_<topic>.cpp`
- Entry: each file exports `int test_<topic>(void)` — `0` pass, non-zero fail
- Runner: `host/main.cpp` lists every test in one table (single binary `host_tests`)

**ST**

- Sources keep the `st_` / `st16_` prefix (linked into one TOS binary)
- Built with `make st-tests` (see [`st_suite/readme.md`](st_suite/readme.md))

## Commands

```bash
cd tiberiandawn
make tests      # build + run all host tests
make st-tests   # build Atari ST suite binary only
```
