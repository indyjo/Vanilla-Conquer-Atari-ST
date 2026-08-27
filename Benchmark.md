# C&C4ST playback benchmark

Format modelled on [STDOOM Benchmark.txt](https://github.com/indyjo/STDOOM/blob/master/Benchmark.txt).

## How to run

Playback benchmark (`RECORD.BIN` in the game directory). TOS cannot take argv; rename `cnc.tos` to `cnc.ttp` so GEMDOS will pass the flags:

```text
cnc.ttp -XY          # default audio
cnc.ttp -XYQ         # mostly disable audio (-XQ)
```

`-XQ` turns off theme/score and some init paths; **SFX and EVA from the recording still play** (see Vanilla TD `Debug_Quiet` behaviour).

Intro / briefing cutscenes are not part of the timed run. Abort them with **ESC**, or delete `MOVIES.MIX` so they never start.

### Autotune (0.3.3+)

From **0.3.3**, a boot CPU probe turns on idle-animation throttles and AI freeze during map gestures on ≤16 MHz-class machines (8 MHz ST/STe and 16 MHz Mega STe). That cuts fidget redraws, so the published ST/STe fps numbers **include** autotune. Construction buildup/sell frames stay on unless you set `SkipBuildingConstructionAnims=1`. Falcon/TT are above the probe threshold and are unaffected.

For a true system-performance run (full idle anims; AI keeps simulating while you scroll), put this in `CONQUER.INI` `[AtariST]`:

```ini
[AtariST]
ThrottleBuildingIdleAnims=0
ThrottleInfantryIdleAnims=0
SkipBuildingConstructionAnims=0
FreezeAIDuringMapGestures=0
```

The game rewrites `CONQUER.INI` when you leave the **Options** menu (Resume), when you OK **Game Controls** or **Sound**, and once at startup (`PlayIntro=No`). `[AtariST]` keys are kept on those writes (legacy copies under `[Options]` are migrated).

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
- **0.3.1** — soft/HW blit from [PR #7](https://github.com/indyjo/Vanilla-Conquer-Atari-ST/pull/7); AUDX fix (no GEMDOS in VBL) (2026-08-05)
- **0.3.2** — drop BLiTTER cache-sync; soft blit on 040+ / TT-RAM; keep HW blit on 030 (2026-08-07)
- **0.3.3** — coalesced clipped redraw (CCR), partial HidPage present, idle-anim throttles (2026-08-20)
- **0.3.4** — YM-2149 audio support, link-time optimization (LTO) (2026-08-27)

## Results

### 8 MHz Atari ST (68000), 4 MB RAM (no BLiTTER)

`-XY` (YM-2149 digi auto-enabled from **0.3.4**; no earlier ST `-XY` row):

| Version | Ticks | Time | FPS |
|---------|-------|------|-----|
| 0.3.4 | 163561 | 13:37.80 | 1.9198 |

`-XYQ`:

| Version | Ticks | Time | FPS |
|---------|-------|------|-----|
| 0.3.0 | 255603 | 21:18.01 | 1.2285 |
| 0.3.1 | 153904 | 12:49.52 | 2.0402 |
| 0.3.2 | 141694 | 11:48.47 | 2.2160 |
| 0.3.3 | 99945 | 8:19.72 | 3.1417 |
| 0.3.4 | 92515 | 7:42.57 | 3.3940 |

`0.3.1` is about **65%** faster than `0.3.0` on plain ST (`-XYQ`). `0.3.3` is about **42%** faster than `0.3.2` (and about **54%** faster than `0.3.1`). `0.3.4` is about **8%** faster than `0.3.3` on ST `-XYQ`. With YM digi auto-on, ST `-XY` runs at about **57%** of ST `-XYQ` fps.

### 8 MHz Atari STe (68000), EmuTOS 1.3 (US), 60 Hz (emulated)

`-XY`:

| Version | Ticks | Time | FPS |
|---------|-------|------|-----|
| 0.1.1 | 161245 | 13:26.22 | 1.9473 |
| 0.2.0 | 158918 | 13:14.59 | 1.9759 |
| 0.3.0 | 158546 | 13:12.73 | 1.9805 |
| 0.3.1 | 158929 | 13:14.64 | 1.9757 |
| 0.3.2 | 156058 | 13:00.29 | 2.0121 |
| 0.3.3 | 105805 | 8:49.02 | 2.9677 |
| 0.3.4 | 97843 | 8:09.21 | 3.2092 |

`-XYQ`:

| Version | Ticks | Time | FPS |
|---------|-------|------|-----|
| 0.1.1 | 132919 | 11:04.59 | 2.3623 |
| 0.2.0 | 131036 | 10:55.18 | 2.3963 |
| 0.3.0 | 130331 | 10:51.65 | 2.4093 |
| 0.3.1 | 130657 | 10:53.28 | 2.4032 |
| 0.3.2 | 128066 | 10:40.33 | 2.4519 |
| 0.3.3 | 84882 | 7:04.41 | 3.6993 |
| 0.3.4 | 75097 | 6:15.48 | 4.1813 |

STe `-XYQ` uses default `ST16_USE_PRESHIFT=0`; soft-blit gains show mainly on plain ST. `0.3.3` is about **51%** faster than `0.3.2` on STe `-XYQ`. `0.3.4` is about **8%** faster than `0.3.3` on STe `-XY` and about **13%** faster on STe `-XYQ`.

### 16 MHz Atari Falcon (68030), EmuTOS 1.3 512 KB (US), 60 Hz (emulated)

`-XY`:

| Version | Ticks | Time | FPS |
|---------|-------|------|-----|
| 0.1.1 | 49567 | 4:07.83 | 6.3349 |
| 0.2.0 | 49253 | 4:06.26 | 6.3752 |
| 0.3.0 | 49790 | 4:08.95 | 6.3065 |
| 0.3.1 | 50478 | 4:12.39 | 6.2205 |
| 0.3.2 | 46995 | 3:54.97 | 6.6816 |
| 0.3.3 | 36298 | 3:01.49 | 8.6506 |
| 0.3.4 | 32269 | 2:41.34 | 9.7307 |

`-XYQ`:

| Version | Ticks | Time | FPS |
|---------|-------|------|-----|
| 0.1.1 | 47885 | 3:59.42 | 6.5574 |
| 0.2.0 | 47339 | 3:56.69 | 6.6330 |
| 0.3.0 | 47231 | 3:56.15 | 6.6482 |
| 0.3.1 | 47990 | 3:59.95 | 6.5430 |
| 0.3.2 | 44044 | 3:40.22 | 7.1292 |
| 0.3.3 | 33982 | 2:49.91 | 9.2402 |
| 0.3.4 | 29991 | 2:29.95 | 10.4698 |

`0.3.4` is about **12%** faster than `0.3.3` on this Falcon (`-XY`) and about **13%** faster (`-XYQ`).

#### Same Falcon, TT-RAM, no BLiTTER (software blits)

EmuTOS reports no blitter (`AllowHardwareBlitFills` forced off → software path; hidden page / shadow cache may sit in TT-RAM via `Pref_Ttram_Alloc`). `-XYQ`:

| Version | Ticks | Time | FPS |
|---------|-------|------|-----|
| 0.3.1 | 33843 | 2:49.21 | 9.2781 |
| 0.3.2 | 31182 | 2:35.91 | 10.0699 |
| 0.3.3 | 24059 | 2:00.29 | 13.0512 |
| 0.3.4 | 22492 | 1:52.46 | 13.9605 |

`0.3.4` is about **7%** faster than `0.3.3` on this Falcon with TT-RAM (`-XYQ`).

### 32 MHz Atari TT (68030), 4 MB ST-RAM + 4 MB TT-RAM, EmuTOS 1.3

`-XYQ`:

| Version | Ticks | Time | FPS |
|---------|-------|------|-----|
| 0.3.2 | 15387 | 1:16.93 | 20.4068 |
| 0.3.3 | 11904 | 0:59.52 | 26.3777 |
| 0.3.4 | 11103 | 0:55.51 | 28.2806 |

`0.3.3` is about **29%** faster than `0.3.2` on this TT (`-XYQ`). `0.3.4` is about **7%** faster than `0.3.3`.
