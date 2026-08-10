# Curated blobs for remix host regression tests.

## Shapes (`make test-shpx`)

Regenerate from an unmodified `CONQUER.MIX`:

```bash
python3 testdata/extract_shapes.py "/path/to/conquer.mix"
```

| Shape | Role |
|-------|------|
| 50CAL.SHP | Tiny single-frame |
| POWER.SHP | Tiny multi-frame UI |
| BOMB.SHP | Small effect |
| MINIGUN.SHP | XOR / delta chain (spot frames) |
| OPTIONS.SHP | Medium dialog chrome |
| SMOKE_M.SHP | Medium animated smoke |
| RADAR.GDI | Large radar asset |
| TREX.SHP | Large unit (spot frames) |
| E4.SHP | Long XOR chain, many frames (spot frames) |
| TRANS.ICN | Negative: ShapeBlock, not KeyFrame |

## Iconsets (`make test-st16`)

Regenerate from retail CD `TEMPERAT.MIX` (GDI/NOD, standard 8bpp, not already ST16):

```bash
python3 testdata/extract_iconsets.py "/path/to/temperat.mix"
```

| Iconset | Role |
|---------|------|
| P04.TEM | Retail CD debris (`Count=103`, 2 images, `TransFlag=[1,0]`, one key pixel at (4,10)). Convert must keep that layout and emit a correct BE mask (not LE word-swapped right-half transparency). |
