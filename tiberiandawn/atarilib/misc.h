/*
 * misc.h - Miscellaneous functions for Atari ST/MiNT
 */

#ifndef MISC_H
#define MISC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Random number generation */
int IRandom(int minval, int maxval);

/* Program exit function */
void __cdecl Prog_End(const char *why = NULL, bool fatal = false);

#ifdef __cplusplus
}
#endif

#endif /* MISC_H */
