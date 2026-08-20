/*
 * st_autotune.cpp — 200 Hz bogomips-style probe and idle-anim throttle flags.
 */

#ifdef ATARI_ST

#include "st_autotune.h"

#include <stdio.h>
#include <string.h>

#include "function.h"
#include "common/ini.h"

bool ThrottleBuildingIdleAnims = false;
bool ThrottleInfantryIdleAnims = false;
bool SkipBuildingConstructionAnims = false;
bool FreezeAIDuringMapGestures = false;

static int g_ini_throttle_building = -1;
static int g_ini_throttle_infantry = -1;
static int g_ini_skip_buildup = -1;
static int g_ini_freeze_gestures = -1;

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

void ST_Autotune_Configure(int building_ini, int infantry_ini, int skip_buildup_ini, int freeze_gestures_ini)
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
	SkipBuildingConstructionAnims = St_Autotune_Apply_Override(skip_buildup_ini, false);
	FreezeAIDuringMapGestures = St_Autotune_Apply_Override(freeze_gestures_ini, slow_16mhz);
	DBG_INFO("Building idle animations are %s.",
		ThrottleBuildingIdleAnims ? "throttled" : "not throttled");
	DBG_INFO("Infantry idle animations are %s.",
		ThrottleInfantryIdleAnims ? "throttled" : "not throttled");
	DBG_INFO("Building construction animations are %s.",
		SkipBuildingConstructionAnims ? "skipped (static frame)" : "played");
	DBG_INFO("AI freeze during map gestures is %s.",
		FreezeAIDuringMapGestures ? "on" : "off");
	DBG_INFO("To change these, edit CONQUER.INI [%s]: ThrottleBuildingIdleAnims / ThrottleInfantryIdleAnims / FreezeAIDuringMapGestures: 0 = off, 1 = on, omit or -1 = auto-detect. SkipBuildingConstructionAnims: 1 = skip (static frame), omit / -1 / 0 = play.",
		ST_AUTOTUNE_INI_SECTION);
}

void ST_Autotune_Remember_INI(int building, int infantry, int skip, int freeze)
{
	g_ini_throttle_building = building;
	g_ini_throttle_infantry = infantry;
	g_ini_skip_buildup = skip;
	g_ini_freeze_gestures = freeze;
}

static int St_Autotune_Clamp_Ini(int value)
{
	if (value < 0) {
		return -1;
	}
	return value != 0 ? 1 : 0;
}

/* Dummy value so Put_String will store a ';' comment line (Save omits =value). */
static char const k_comment_mark[] = "x";

/*
 * Each ';' line is also an INI entry name, so every comment string must be unique.
 * Shape: what the option does, not the key name. Max 40 columns.
 */
static char const* const k_section_comments[] = {
	"; ST extra options.",
	"; 0=off 1=on -1=auto",
	0,
};

struct St_Autotune_Ini_Item {
	char const* comments[3];
	char const* key;
};

static St_Autotune_Ini_Item const k_ini_items[] = {
	{ { "; Fewer flags, fans, smoke.", 0, 0 },
	  "ThrottleBuildingIdleAnims" },
	{ { "; Fewer fidgets or salutes.", 0, 0 },
	  "ThrottleInfantryIdleAnims" },
	{ { "; No crane/buildup movie.", 0, 0 },
	  "SkipBuildingConstructionAnims" },
	{ { "; Game stops while scrolling.", 0, 0 },
	  "FreezeAIDuringMapGestures" },
};

enum { ST_AUTOTUNE_INI_ITEM_COUNT = sizeof(k_ini_items) / sizeof(k_ini_items[0]) };

static int const* St_Autotune_Stored_Slots(void)
{
	static int slots[ST_AUTOTUNE_INI_ITEM_COUNT];
	slots[0] = g_ini_throttle_building;
	slots[1] = g_ini_throttle_infantry;
	slots[2] = g_ini_skip_buildup;
	slots[3] = g_ini_freeze_gestures;
	return slots;
}

static void St_Autotune_Put_Comment(INIClass& ini, char const* line)
{
	ini.Put_String(ST_AUTOTUNE_INI_SECTION, line, k_comment_mark);
}

static void St_Autotune_Put_Commented_Keys(INIClass& ini, int const* values)
{
	static char const* const ATARI = ST_AUTOTUNE_INI_SECTION;
	unsigned c;

	for (c = 0; k_section_comments[c] != 0; ++c) {
		St_Autotune_Put_Comment(ini, k_section_comments[c]);
	}
	for (unsigned i = 0; i < ST_AUTOTUNE_INI_ITEM_COUNT; ++i) {
		for (c = 0; c < 3 && k_ini_items[i].comments[c] != 0; ++c) {
			St_Autotune_Put_Comment(ini, k_ini_items[i].comments[c]);
		}
		ini.Put_Int(ATARI, k_ini_items[i].key, values[i]);
	}
}

void ST_Autotune_Preserve_INI(INIClass& ini)
{
	static char const* const ATARI = ST_AUTOTUNE_INI_SECTION;
	static char const* const OPTIONS = "Options";
	int const* const stored = St_Autotune_Stored_Slots();
	int values[ST_AUTOTUNE_INI_ITEM_COUNT];

	for (unsigned i = 0; i < ST_AUTOTUNE_INI_ITEM_COUNT; ++i) {
		char const* const key = k_ini_items[i].key;
		int value = stored[i];
		if (ini.Is_Present(ATARI, key)) {
			value = ini.Get_Int(ATARI, key, value);
		} else if (ini.Is_Present(OPTIONS, key)) {
			value = ini.Get_Int(OPTIONS, key, value);
		}
		values[i] = St_Autotune_Clamp_Ini(value);
		ini.Clear(OPTIONS, key);
	}
	ini.Clear(ATARI);
	St_Autotune_Put_Commented_Keys(ini, values);
}

void ST_Autotune_Append_Section_If_Missing(char* profile)
{
	int const* const stored = St_Autotune_Stored_Slots();
	char block[700];
	char* p;
	char* end;
	unsigned i;
	unsigned c;

	if (profile == NULL || strstr(profile, "[" ST_AUTOTUNE_INI_SECTION "]") != NULL) {
		return;
	}
	p = block;
	end = block + sizeof(block);
	p += sprintf(p, "\r\n[" ST_AUTOTUNE_INI_SECTION "]\r\n");
	for (c = 0; k_section_comments[c] != 0 && p + 48 < end; ++c) {
		p += sprintf(p, "%s\r\n", k_section_comments[c]);
	}
	for (i = 0; i < ST_AUTOTUNE_INI_ITEM_COUNT && p + 80 < end; ++i) {
		for (c = 0; c < 3 && k_ini_items[i].comments[c] != 0; ++c) {
			p += sprintf(p, "%s\r\n", k_ini_items[i].comments[c]);
		}
		p += sprintf(p, "%s=%d\r\n", k_ini_items[i].key, St_Autotune_Clamp_Ini(stored[i]));
	}
	strcat(profile, block);
}

#endif /* ATARI_ST */
