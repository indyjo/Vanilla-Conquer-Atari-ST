# Curated CONQUER.MIX shape blobs for `make test-shpx`.

Regenerate from an unmodified CONQUER.MIX:

```bash
python3 testdata/extract_shapes.py "/path/to/conquer.mix"
```

Files:

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
