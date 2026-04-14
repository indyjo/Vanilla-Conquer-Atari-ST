#ifndef ST_BUILD_FRAME_ASSETS_H
#define ST_BUILD_FRAME_ASSETS_H

/* Returns count of failed Build_Frame calls (0 = all attempted decodes succeeded). */
int st_run_build_frame_asset_autocheck(void);

/* Low rez: menu-pick SHP, Build_Frame all frames tiled; silent Y/Z vs N on preview. */
int st_run_interactive_build_frame_xor_grid(void);

#endif
