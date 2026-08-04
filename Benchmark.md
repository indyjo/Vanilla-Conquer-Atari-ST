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
- **0.3.0** — AUDX pools + page cache; 8-bit PCM stream / LUT path (2026-07-30)
- **0.3.1** — faster software blitter and related Falcon/STE fixes from [PR #7](https://github.com/indyjo/Vanilla-Conquer-Atari-ST/pull/7) (Matthias Alles); BLiTTER wait uses premature restart on ST/STe and a plain `BUSY` poll on Falcon (2026-08-04)

## Results

### 8 MHz Atari ST (68000), 4 MB RAM (no BLiTTER)

Same 1570-frame playback recording (software blits only):

| Version | Ticks | Time | FPS |
|---------|-------|------|-----|
| 0.3.0 | 255603 | 21:18.01 | 1.2285 |
| 0.3.1 | 166924 | 13:54.62 | 1.8811 |

`0.3.1` is about **53%** faster than `0.3.0` on plain ST (`-XYQ`).

### 8 MHz Atari STe (68000), EmuTOS 1.3 (US), 60 Hz (emulated)

`-XY`:

| Version | Ticks | Time | FPS |
|---------|-------|------|-----|
| 0.1.1 | 161245 | 13:26.22 | 1.9473 |
| 0.2.0 | 158918 | 13:14.59 | 1.9759 |
| 0.3.0 | 158546 | 13:12.73 | 1.9805 |
| 0.3.1 | 151238 | 12:36.19 | 2.0762 |

`-XYQ`:

| Version | Ticks | Time | FPS |
|---------|-------|------|-----|
| 0.1.1 | 132919 | 11:04.59 | 2.3623 |
| 0.2.0 | 131036 | 10:55.18 | 2.3963 |
| 0.3.0 | 130331 | 10:51.65 | 2.4093 |
| 0.3.1 | 128594 | 10:42.97 | 2.4418 |

### 16 MHz Atari Falcon (68030), EmuTOS 1.3 512 KB (US), 60 Hz (emulated)

`-XY`:

| Version | Ticks | Time | FPS |
|---------|-------|------|-----|
| 0.1.1 | 49567 | 4:07.83 | 6.3349 |
| 0.2.0 | 49253 | 4:06.26 | 6.3752 |
| 0.3.0 | 49790 | 4:08.95 | 6.3065 |
| 0.3.1 | 49712 | 4:08.56 | 6.3164 |

`-XYQ`:

| Version | Ticks | Time | FPS |
|---------|-------|------|-----|
| 0.1.1 | 47885 | 3:59.42 | 6.5574 |
| 0.2.0 | 47339 | 3:56.69 | 6.6330 |
| 0.3.0 | 47231 | 3:56.15 | 6.6482 |
| 0.3.1 | 47197 | 3:55.98 | 6.6530 |

#### Same Falcon, TT-RAM, no BLiTTER (software blits)

EmuTOS reports no blitter (`AllowHardwareBlitFills` forced off → software path; hidden page / shadow cache may sit in TT-RAM via `Pref_Ttram_Alloc`). `-XYQ`:

| Version | Ticks | Time | FPS |
|---------|-------|------|-----|
| 0.3.1 | 37029 | 3:05.14 | 8.4798 |

