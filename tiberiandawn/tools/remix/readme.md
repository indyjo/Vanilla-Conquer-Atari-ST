# remix

Repack C&C MIX archives for the Atari ST port.

For each embedded file the tool autodetects the asset type, converts audio to
11025 Hz 8-bit mono PCM `.AUD` where needed (IMA99, Westwood compression type 1,
PCM stereo/16-bit/other rates), and pads payloads so every file starts at an even
byte offset from the beginning of the MIX.

Plain TD-style MIX files only (no encrypted or extended headers).

## Build

Host utility:

```bash
cd tiberiandawn/tools/remix
make
```

MiNT `remix.tos` (cross-compiler):

```bash
make mint
```

The `itch-release` target in `tiberiandawn/makefile` builds `remix.tos` and
includes it in the release zip next to `cnc.tos`.

## Host usage

**Single file** (output path must differ from input):

```bash
./remix -o output.mix input.mix
```

**Directory** (non-recursive; `.mix` / `.MIX`):

```bash
./remix -d /path/to/gamedata
```

## MiNT usage (`remix.tos`)

No arguments. Place `remix.tos` in the game folder with the `.mix` files and
run once before `cnc.tos`. The tool writes `temp.mxx` while working on each
archive, then replaces the source `.mix` in place.

## Options (host only)

| Option | Meaning |
|--------|---------|
| `-o`, `--output PATH` | Output MIX file, or output directory with `-d` |
| `-d`, `--directory DIR` | Process all MIX files in `DIR` |
| `-h`, `--help` | Show help |
