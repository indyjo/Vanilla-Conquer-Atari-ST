#ifndef ST_FONT_BROWSER_H
#define ST_FONT_BROWSER_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Browse .FNT entries catalogued in mix.txt / mix2.txt (CCLOCAL.MIX, UPDATE.MIX,
 * UPDATEC.MIX, and DOS local.mix as LOCAL.MIX / local.mix). Requires those MIX files in cwd
 * where applicable.
 */
int st_run_interactive_font_browser(void);

#ifdef __cplusplus
}
#endif

#endif
