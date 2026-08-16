/*
 * st_autotune.cpp — 200 Hz bogomips-style probe and idle-anim throttle flags.
 */

#ifdef ATARI_ST

#include "st_autotune.h"

#include "function.h"

bool ThrottleBuildingIdleAnims = false;
bool ThrottleInfantryIdleAnims = false;
bool SkipBuildingConstructionAnims = false;

/*
 * Sample window in _hz_200 ticks (200 Hz). 40 ticks = 200 ms.
 * Keep this fixed when calibrating; only change the threshold below.
 */
enum { ST_AUTOTUNE_PROBE_TICKS = 40 };

/*
 * Tune this for ≤16MHz vs faster (32MHz / 030 cache, etc.).
 *
 * Calibrated on a stock 8MHz STe: loops=275 in 40 ticks. A 16MHz 68000
 * (Mega STE) should land near ~550. Machines with loops <= this value
 * enable both throttles (unless INI overrides).
 *
 * At boot, cnc.log prints:
 *   CPU probe: loops=<N> in 40 ticks (16MHz max=<this>)
 *
 * Raise ST_AUTOTUNE_16MHZ_MAX_LOOPS if a 16MHz box (especially Falcon 030)
 * logs above it and should still throttle; lower it if a 32MHz machine
 * still throttles.
 */
enum { ST_AUTOTUNE_16MHZ_MAX_LOOPS = 800UL };

enum { ST_HZ200_ADDR = 0x4BA };

static unsigned long __attribute__((noinline)) St_Autotune_Probe_Loops(void)
{
	volatile unsigned long sink = 1;
	volatile unsigned long const* const hz200 = (volatile unsigned long*)ST_HZ200_ADDR;
	unsigned long loops = 0;
	unsigned long t0 = *hz200;

	while (*hz200 == t0) {
	}
	t0 = *hz200;

	do {
		unsigned long i;
		for (i = 0; i < 64UL; ++i) {
			sink += (sink << 1) ^ i;
		}
		++loops;
	} while ((*hz200 - t0) < (unsigned long)ST_AUTOTUNE_PROBE_TICKS);

	(void)sink;
	return loops;
}

static bool St_Autotune_Apply_Override(int ini, bool auto_on)
{
	if (ini < 0) {
		return auto_on;
	}
	return ini != 0;
}

void ST_Autotune_Configure(int building_ini, int infantry_ini, int skip_buildup_ini)
{
	unsigned long const loops = St_Autotune_Probe_Loops();
	bool const slow_16mhz = (loops <= (unsigned long)ST_AUTOTUNE_16MHZ_MAX_LOOPS);

	DBG_INFO("CPU probe: loops=%lu in %d ticks (16MHz max=%lu) -> %s",
		loops,
		(int)ST_AUTOTUNE_PROBE_TICKS,
		(unsigned long)ST_AUTOTUNE_16MHZ_MAX_LOOPS,
		slow_16mhz ? "16MHz-class or slower" : "faster");

	ThrottleBuildingIdleAnims = St_Autotune_Apply_Override(building_ini, slow_16mhz);
	ThrottleInfantryIdleAnims = St_Autotune_Apply_Override(infantry_ini, slow_16mhz);
	SkipBuildingConstructionAnims = St_Autotune_Apply_Override(skip_buildup_ini, slow_16mhz);
	DBG_INFO("Building idle animations are %s.",
		ThrottleBuildingIdleAnims ? "throttled" : "not throttled");
	DBG_INFO("Infantry idle animations are %s.",
		ThrottleInfantryIdleAnims ? "throttled" : "not throttled");
	DBG_INFO("Building construction animations are %s.",
		SkipBuildingConstructionAnims ? "skipped (static frame)" : "played");
	DBG_INFO("To change these, edit CONQUER.INI [Options]: ThrottleBuildingIdleAnims / ThrottleInfantryIdleAnims / SkipBuildingConstructionAnims. 0 = off, 1 = on. Omit them, or use -1, to keep this auto-detect.");
}

#endif /* ATARI_ST */
