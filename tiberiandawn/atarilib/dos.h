/*
 * dos.h - DOS-specific functions compatibility header for Atari ST/MiNT
 * 
 * Provides DOS/BIOS function stubs. Most DOS functions are not applicable
 * on Atari ST/MiNT and should be replaced with platform-specific equivalents.
 */

#ifndef DOS_H
#define DOS_H

#ifdef __cplusplus
extern "C" {
#endif

/* DOS interrupt and port I/O functions - not applicable on Atari ST */
/* These are stubs that may need platform-specific implementations */

/* Far pointer macros - not needed on flat memory model (m68k) */
#define MK_FP(seg, off) ((void*)(off))
#define FP_SEG(fp) ((unsigned short)0)
#define FP_OFF(fp) ((unsigned short)(unsigned long)(fp))

/* Interrupt functions - not applicable, provide stubs */
static inline void geninterrupt(int interrupt) { (void)interrupt; }
static inline void disable(void) { }
static inline void enable(void) { }

/* Port I/O - not applicable on Atari ST, provide stubs */
static inline void outport(unsigned short port, unsigned short value) { (void)port; (void)value; }
static inline unsigned short inport(unsigned short port) { (void)port; return 0; }
static inline void outportb(unsigned short port, unsigned char value) { (void)port; (void)value; }
static inline unsigned char inportb(unsigned short port) { (void)port; return 0; }

/* DOS file functions - use standard C/POSIX equivalents instead */
/* These are typically not used in main source code */

#ifdef __cplusplus
}
#endif

#endif /* DOS_H */

