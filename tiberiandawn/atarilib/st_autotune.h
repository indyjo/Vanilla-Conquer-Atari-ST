/*
 * Startup CPU auto-tune (200 Hz probe) and idle-anim throttle flags.
 *
 * ThrottleBuildingIdleAnims / ThrottleInfantryIdleAnims / SkipBuildingConstructionAnims /
 * FreezeAIDuringMapGestures are set at startup from a 200 Hz CPU probe, then optionally
 * overridden by CONQUER.INI.
 */

#ifndef ST_AUTOTUNE_H
#define ST_AUTOTUNE_H

#ifdef ATARI_ST

#ifdef __cplusplus
extern "C" {
#endif

extern bool ThrottleBuildingIdleAnims;
extern bool ThrottleInfantryIdleAnims;
extern bool SkipBuildingConstructionAnims;
extern bool FreezeAIDuringMapGestures;

/* Idle anims play this many times less often when throttling is on. */
enum { ST_IDLE_ANIM_THROTTLE_FACTOR = 4 };

/*
 * *_ini from CONQUER.INI [Options]:
 *   -1 = auto (CPU probe), 0 = force off, 1 = force on.
 * Call after Super() so _hz_200 at 0x4BA is readable.
 */
void ST_Autotune_Configure(int building_ini, int infantry_ini, int skip_buildup_ini, int freeze_gestures_ini);

#ifdef __cplusplus
}
#endif

#endif /* ATARI_ST */

#endif /* ST_AUTOTUNE_H */
