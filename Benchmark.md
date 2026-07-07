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

- **0.1.1** — baseline (2026-06-25)
- **0.2.0** — benchmark refresh (2026-07-07)

<!-- Add new lines here as optimizations land, e.g.:
- 0.3.0 — …
- 0.4.0 — …
-->

## Results

### 8 MHz Atari STe (68000), EmuTOS 1.3 (US), 60 Hz (emulated)

`-XY`:

| Version | Ticks | Time | FPS |
|---------|-------|------|-----|
| 0.1.1 | 161245 | 13:26.22 | 1.9473 |
| 0.2.0 | 158918 | 13:14.59 | 1.9759 |

`-XYQ`:

| Version | Ticks | Time | FPS |
|---------|-------|------|-----|
| 0.1.1 | 132919 | 11:04.59 | 2.3623 |
| 0.2.0 | 131036 | 10:55.18 | 2.3963 |

`0.2.0` improves the 8 MHz STe benchmark by about **1.4%** versus `0.1.1` in both audio and `-XQ` modes.

### 16 MHz Atari Falcon (68030), EmuTOS 1.3 512 KB (US), 60 Hz (emulated)

`-XY`:

| Version | Ticks | Time | FPS |
|---------|-------|------|-----|
| 0.1.1 | 49567 | 4:07.83 | 6.3349 |
| 0.2.0 | 49253 | 4:06.26 | 6.3752 |

`-XYQ`:

| Version | Ticks | Time | FPS |
|---------|-------|------|-----|
| 0.1.1 | 47885 | 3:59.42 | 6.5574 |
| 0.2.0 | 47339 | 3:56.69 | 6.6330 |

`0.2.0` improves the Falcon benchmark by about **0.6%** with audio and **1.1%** in `-XQ` mode versus `0.1.1`.

<!-- Future sections (fill in when measured), e.g.:

### 8 MHz Atari STe, TOS 1.62, 50 Hz

### Real hardware
-->
