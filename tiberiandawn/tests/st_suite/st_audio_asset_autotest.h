#ifndef ST_AUDIO_ASSET_AUTOTEST_H
#define ST_AUDIO_ASSET_AUTOTEST_H

#include <stddef.h>

/* First asset found in built-in try order (for automated bundle / menu item 8). Requires supervisor on MiNT ($484 mute). */
int st_run_asset_audio_autotest(void);

/* verbose: print PASS line on success. Returns ST_AUDIO_RESULT_* (0 pass, 1 fail, 2 skip). */
int st_run_asset_audio_autotest_ex(int verbose);

/* Number of selectable rows in the audio submenu (fixed list in .cpp). */
int st_asset_audio_try_count(void);

/* One-line label for submenu row idx (0-based). buf[0]= on bad idx. */
void st_asset_audio_try_label(int idx, char *buf, size_t buflen);

/* Play exactly that row; missing MIX = skip (0). -1 = bad idx. Requires supervisor on MiNT ($484 mute). */
int st_run_asset_audio_try_index(int idx);

/*
 * TOS conterm ($484) bit 0 = keyboard click. MiNT often faults $484 from user mode;
 * call push/pop only while supervisor (e.g. whole body under Super(0L)).
 */
void st_conterm_keyclick_mute_push(void);
void st_conterm_keyclick_mute_pop(void);

#endif
