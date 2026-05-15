/*
 * st_border_profile.h - STE pen-0 border/background timing (Hatari / logic analyzer).
 */
#ifndef ST_BORDER_PROFILE_H
#define ST_BORDER_PROFILE_H

#if defined(ST_BORDER_PROFILE)

#define ST_BORDER_PALETTE_REGS ((volatile unsigned short*)0xFF8240u)
#define BORDER_COLOR(c) \
	unsigned short old_color = ST_BORDER_PALETTE_REGS[0]; \
	ST_BORDER_PALETTE_REGS[0] = (unsigned short)(c)
#define BORDER_RESTORE() (ST_BORDER_PALETTE_REGS[0] = old_color)

#else

#define BORDER_COLOR(c) ((void)(c))
#define BORDER_RESTORE() ((void)0)

#endif

#endif /* ST_BORDER_PROFILE_H */
