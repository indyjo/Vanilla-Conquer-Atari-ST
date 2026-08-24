/*
 * Startup CPU auto-tune (200 Hz probe) and idle-anim throttle flags.
 *
 * ThrottleBuildingIdleAnims / ThrottleInfantryIdleAnims / FreezeAIDuringMapGestures
 * are set at startup from a 200 Hz CPU probe, then optionally overridden by CONQUER.INI.
 * SkipBuildingConstructionAnims defaults to off (play buildup/sell frames); INI 1 forces skip.
 * Audio= / StvqEnableAudio= are remembered here so Options saves preserve [AtariST].
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

/*
 * *_ini from CONQUER.INI [Options]:
 *   -1 = auto (CPU probe) for idle throttles and gesture freeze; for
 *        SkipBuildingConstructionAnims, -1 = off (play anims).
 *   0 = force off, 1 = force on.
 * Call after Super() so _hz_200 at 0x4BA is readable.
 */
void ST_Autotune_Configure(int building_ini, int infantry_ini, int skip_buildup_ini, int freeze_gestures_ini);

#ifdef __cplusplus
}
#endif

#define ST_AUTOTUNE_INI_SECTION "AtariST"

/*
 * Keep [AtariST] throttle keys (including -1 auto) across CONQUER.INI
 * rewrites. Regenerates short ';' comments (Load discards comments).
 */
void ST_Autotune_Remember_INI(int building, int infantry, int skip, int freeze);
void ST_Autotune_Remember_Audio_INI(char const* audio, int stvq_enable_audio);
void ST_Autotune_Preserve_INI(class INIClass& ini);
/* First-boot / PlayIntro rewrite: append [AtariST] if the profile lacks it. */
void ST_Autotune_Append_Section_If_Missing(char* profile);

#endif /* ATARI_ST */

#endif /* ST_AUTOTUNE_H */
