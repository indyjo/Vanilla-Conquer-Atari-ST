/*
 * Console output helpers for ST test harness (40-column terminal discipline).
 */
#ifndef ST_TEXT_H
#define ST_TEXT_H

#ifdef __cplusplus
extern "C" {
#endif

/** Default wrap width for suite output */
#define ST_TEXT_MAXCOL 40

/** Print paragraph with word-wrap; lines never exceed maxcol characters. */
void st_wrap_puts(const char *paragraph, int maxcol);

/** Crawcin-based Y/N (Y or Z = ok for QWERTZ). Returns 1 = ok, 0 = bad. */
int st_read_yes_no(void);

#ifdef __cplusplus
}
#endif

#endif
