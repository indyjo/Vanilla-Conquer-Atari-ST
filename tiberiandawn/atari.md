# Atari ST Notes

This file collects Atari ST port specific implementation notes.

## W16 Weightset Format

`*.W16` is a raw binary C2P weight table: `C2P_Install_CustomWeights()` reads it once to rebuild all derived LUTs; C2P does not retain the weight matrix or a pointer to it.

- Total size: `4096` bytes (`256 * 16`)
- Layout: 256 consecutive rows, one per 8-bit source palette index (`0..255`)
- Row size: 16 bytes, one weight for each ST color index (`0..15`)
- Row sum rule: each row should sum to `16` (4x4 Bayer total weight budget)
- Byte type: unsigned 8-bit integers

Equivalent C layout:

```c
uint8_t weights[256][16];
```

For a source palette index `src` and ST color `k`, the weight byte is at:

```text
offset = src * 16 + k
```

## Runtime Usage In ST WSA Test

The interactive WSA test (`tests/st_suite/st_wsa_playback.cpp`) tries to load `X.W16` before playing `X.WSA`.

- If found and valid (4096 bytes), it installs via `C2P_Install_CustomWeights()` (synchronous bake; no retained pointer).
- If not found, it prints a warning and falls back to built-in C2P weights.

## Regenerating W16 Files

Build host-side optimizer:

```bash
make -C tools/palette-opt
```

Generate one table from a WSA with embedded palette:

```bash
tools/palette-opt/palette-opt -p path/to/FILE.WSA --dump path/to/FILE.W16
```

Generate one table from a raw 768-byte PAL:

```bash
tools/palette-opt/palette-opt -p path/to/FILE.PAL --dump path/to/FILE.W16
```

The current test-suite WSA set has pre-generated `.W16` files in `bin/AtariST`.
