# histtool

Host utility that builds or transforms 256-index color histograms for use with
`palette-opt --hist`.

## Build

```bash
cd tools/histtool
make
```

## Usage

```bash
./histtool [OPTIONS] FILE [FILE ...]
```

**Inputs** (positional, at least one; all summed):

| Extension | Meaning |
|-----------|---------|
| `.bmp` | 8-bit indexed BMP (BI_RGB or BI_RLE8) |
| `.wsa` | C&C WSA animation (all frames decoded and counted) |
| `.hist`, `.txt` | Sparse histogram: `index count` per line |

**Options:** `-o FILE`, `--dense`, `--add=N`, `--mul=N`, `--div=N`,
`--chart-width=N`, `--no-chart`, `-q`, `-h`

Filters run in command-line order after all inputs are summed. Counts are clamped
to `>= 0` after each step.

## Examples

```bash
./histtool -o ui.hist assets/ui/*.bmp
./histtool -o map.hist EUROPE.WSA AFRICA.WSA
./histtool -o merged.hist stats_a.hist stats_b.hist frame.bmp
./histtool screen.bmp --add=1 --div=2 -o screen_trim.hist
./histtool counts.hist --mul=3 --no-chart -o counts_x3.hist
```

With `palette-opt`:

```bash
./tools/palette-opt/palette-opt -o SCREEN.W16 -p SCREEN.PAL --hist ui.hist --sa-iter=12000
```
