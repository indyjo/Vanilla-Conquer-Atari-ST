#ifndef ST_AUDX_AUTOTEST_H
#define ST_AUDX_AUTOTEST_H

/*
 * AUDX playback smoke tests (page-cache SFX + file-stream score).
 * Returns 0 pass, 1 fail, 2 skip (missing remacked packs / pools / STE).
 */
int st_run_audx_autotest(void);

#endif
