# C&C4ST playback benchmark

Format modelled on [STDOOM Benchmark.txt](https://github.com/indyjo/STDOOM/blob/master/Benchmark.txt).

## How to run

Playback benchmark (`RECORD.BIN` in the game directory):

```text
cnc.tos -XY          # default audio
cnc.tos -XYQ         # mostly disable audio (-XQ)
```

`-XQ` turns off theme/score and some init paths; **SFX and EVA from the recording still play** (see Vanilla TD `Debug_Quiet` behaviour).

Timing starts when the mission loads; results are printed at playback end (console + dialog):

```text
frames: <simulation frames>
ticks: <200 Hz counter>
time: <min:sec.hundredths>
fps: <average framerate>
```

Higher fps / lower ticks = faster. Use the **same recording** when comparing versions or machines.

## History of versions under Benchmark

- **V0** — C&C4ST **0.1.1** baseline (2026-06-25)

<!-- Add new lines here as optimizations land, e.g.:
- V1 — …
- V2 — …
-->

## Results

### 8 MHz Atari STe (68000), EmuTOS 1.3 (US), 60 Hz (emulated)

Same `RECORD.BIN` for all rows below (~1570 simulation frames).

```text
  V0 (0.1.1) audio     1570 frames  161245 ticks  13:26.22  -> 1.9473 fps
  V0 (0.1.1) -XQ       1570 frames  132919 ticks  11:04.59  -> 2.3623 fps
```

Audio overhead on V0: **+21.3%** wall time / **−17.6%** fps vs `-XQ` (~0.41 fps).

### 16 MHz Atari Falcon (68030), EmuTOS 1.3 512 KB (US), 60 Hz (emulated)

Same `RECORD.BIN` (~1570 simulation frames).

```text
  V0 (0.1.1) audio     1570 frames   49567 ticks   4:07.83  -> 6.3349 fps
  V0 (0.1.1) -XQ       1570 frames   47885 ticks   3:59.42  -> 6.5574 fps
```

Audio overhead on V0: **+3.5%** wall time / **−3.4%** fps vs `-XQ` (~0.22 fps).

## Cross-machine comparison (V0, same recording)

| Machine | Audio fps | `-XQ` fps | Speedup (`-XQ`) | Audio cost |
|---------|-----------|-----------|-----------------|------------|
| 8 MHz STe 68000 | 1.95 | 2.36 | 1.00× (baseline) | −17.6% fps |
| 16 MHz Falcon 68030 | 6.33 | 6.56 | **2.78×** vs 8 MHz `-XQ` | −3.4% fps |

Clock is 2× faster; achieved **~2.8×** higher fps (68030 + Falcon helps). Audio overhead shrinks from **~21%** of wall time to **~3%** — logic and rendering dominate on faster CPU.

<!-- Future sections (fill in when measured), e.g.:

### 8 MHz Atari STe, TOS 1.62, 50 Hz

### Real hardware
-->
