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

/** Echo one character to stdout (libcmini-safe; do not use putchar() in st-tests). */
void st_console_echo_char(int ch);

/** Wait for one keypress, no prompt, always returns 1. */
int st_read_yes_no(void);

/** Same behavior as st_read_yes_no (kept for call-site compatibility). */
int st_read_yes_no_silent(void);

#ifdef __cplusplus
}
#endif

#endif
