# Atari ST runtime assets

Place the files from this folder **next to the `cnc.tos` binary** when you run or distribute the game. The engine loads them from the same directory as the executable.

These files carry **conversion data for turning 256-color WSA animations into the ST’s 16-color display** (chunky-to-planar / palette mapping weights and related sidecar data). Without them, WSA playback may fall back to generic conversion or warn that companion data is missing.

For format details (for example `.W16` weight tables), see `ATARI.md` in the project root.
