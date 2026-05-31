# remix

Host utility that repacks C&C MIX archives for the Atari ST port.

For each embedded file it autodetects the asset type, converts Westwood AUD99
(IMA ADPCM) to 11 kHz 8-bit mono PCM `.AUD` where needed, and pads payloads so
every file starts at an even byte offset from the beginning of the MIX. This
avoids unaligned 16/32-bit reads on m68k when game code treats payload bytes as
structs.

Processing is linear in total archive size: one file at a time, streaming
pass-through for most assets (AUD99 loads one file at a time for decode).

Plain TD-style MIX files only (no encrypted or extended headers).

## Build

```bash
cd tiberiandawn/tools/remix
make
```

## Usage

**Single file** (output path must differ from input):

```bash
./remix -o output.mix input.mix
```

**Directory** (non-recursive; `.mix` and `.MIX`):

```bash
./remix -d /path/to/gamedata
```

Updates each MIX in place via a temporary file in the same directory, then
`rename()`. Optional copy-out mode:

```bash
./remix -d /path/to/gamedata -o /path/to/outdir
```

## Output

A status table is printed for each embedded file: CRC, old/new offset, old/new
size, and detected type. Converted audio is shown as `aud99 -> aud_pcm11`.

## Options

| Option | Meaning |
|--------|---------|
| `-o`, `--output PATH` | Output MIX file, or output directory with `-d` |
| `-d`, `--directory DIR` | Process all MIX files in `DIR` |
| `-h`, `--help` | Show help |

## Example

```bash
./remix -d ~/CNC
./remix -o scores.new.mix scores.mix
```
