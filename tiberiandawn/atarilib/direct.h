/*
 * direct.h - Directory operations compatibility header for Atari ST/MiNT
 * 
 * Provides Windows-style directory functions that map to POSIX equivalents.
 */

#ifndef DIRECT_H
#define DIRECT_H

#ifdef __cplusplus
extern "C" {
#endif

/* On Unix-like systems (including MiNT), these functions are in unistd.h */
#include <unistd.h>

/* Windows-style function names map to POSIX equivalents */
#define getcwd(buf, size) getcwd(buf, size)
#define chdir(path) chdir(path)

/* Additional Windows-specific functions (if needed) */
int _mkdir(const char *pathname);
int _rmdir(const char *pathname);
char *_getcwd(char *buf, int size);
void _makepath(char *path, const char *drive, const char *dir, const char *fname, const char *ext);

#ifdef __cplusplus
}
#endif

#endif /* DIRECT_H */

