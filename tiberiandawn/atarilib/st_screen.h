/*
 * st_screen.h - Atari ST video: TOS snapshot, shifter sync + line base, game buffer registration.
 * Does not read or write STE palette ($FF8240); use Set_Palette / Fade_Palette_To from game code.
 *
 * ST_SEPARATE_DEBUG_SCREEN (makefile SEPARATE_DEBUG_SCREEN=1): game uses a dedicated planar
 * buffer; hardware line base is set directly; Ctrl+F10 toggles to the captured TOS console.
 * Without it: visible page is TOS Logbase, video is set up via Setscreen(), debug text and
 * game graphics share the same screen memory.
 */

#ifndef ST_SCREEN_H
#define ST_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Call once before shifter / line-base changes (e.g. start of Set_Video_Mode). */
void ST_Screen_Capture_Tos_Video_State(void);

/* Lo-res check, hide VDI cursor. Returns 0 if not ST low. */
int ST_Screen_Enter_LoRes_Game_Video(void);

/* Allocate (if needed) and return the planar pointer VisiblePage should use. */
void *ST_Screen_Register_Game_Visible(int width, int height);

/* Low res ($FF8260) + shifter video base -> game buffer (OS Logbase unchanged). */
void ST_Screen_Apply_Game_Video_Hardware(void);

/* Resolution ($FF8260) + hardware video base to captured TOS state. */
void ST_Screen_Shutdown_Restore_Tos(void);

/* STE line base: physical address only (TOS logical screen unchanged). */
void ST_Screen_Hardware_Set_Phys_Base(void *phys);

/* Ctrl+F10: toggle shifter between TOS console memory + saved rez, and game + low res (silent). */
void ST_Debug_Screen_Service(void);

/* True while the shifter shows the TOS console framebuffer. */
int ST_Debug_Screen_Is_Active(void);

#ifdef __cplusplus
}
#endif

#endif /* ST_SCREEN_H */
