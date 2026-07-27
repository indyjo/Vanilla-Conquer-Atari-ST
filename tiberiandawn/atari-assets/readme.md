# Atari ST runtime assets

Place the files from this folder **next to the `cnc.tos` binary** when you run or distribute the game. The engine loads them from the same directory as the executable.

## Root `*.w16` (theater / UI / WSA)

These files carry **conversion data for turning 256-color WSA animations into the ST’s 16-color display** (chunky-to-planar / palette mapping weights and related sidecar data). Without them, WSA playback may fall back to generic conversion or warn that companion data is missing.

They are also used by `remix` / remix-web for **ST16** theater iconset conversion (`TEMPERAT.W16`, `DESERT.W16`, `WINTER.W16`, …).

## `video/xxxxxxxx.N.w16` (FMV encode sidecars)

CRC-named palette/weight sidecars for **VQA → STVQ** conversion in `remix` (`--convert-vqa`) and remix-web (0.3.x movie sequences). Naming:

```text
video/{crc:08x}.{seg}.w16
```

where `crc` is the lowercase 8-digit Westwood CRC of the MIX entry (e.g. `GDI1.VQA`), and `seg` is the palette segment index.

These are **encode-time only**. The runtime STVQ player embeds palettes in the `.stv` / MIX payload; `video/` need not remain next to `cnc.tos` after remix. Distribution zips include `video/` so remix-web can encode movies; remix-web drops `video/` from the final player archive.

For format details (for example `.W16` weight tables), see `ATARI.md` in the project root.
