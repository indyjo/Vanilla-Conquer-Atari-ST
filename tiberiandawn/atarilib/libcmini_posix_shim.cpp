/*
 * Minimal POSIX compatibility shims for libcmini Atari ST builds.
 *
 * These cover the small subset of APIs still referenced by the shared
 * cross-platform code without pulling in the full MiNT libc.
 *
 * Mintlib already provides all of these, so the whole file compiles away
 * unless LIBCMINI is defined (LIBC_RUNTIME=mintlib would otherwise hit
 * multiple-definition errors at link time, starting with __flshfp).
 */

#ifdef LIBCMINI

#include <mint/osbind.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

extern "C" {

/*
 * Mintlib stdio.h inlines putchar() through __flshfp when the stream buffer is
 * full. libcmini does not ship that helper, so provide a small bridge.
 */
int __flshfp(FILE *stream, int c)
{
	if (!stream) {
		return EOF;
	}
	fflush(stream);
	return fputc(c, stream);
}

static int st_fnmatch_char_eq(char a, char b, int flags)
{
	if (flags & 0x10 /* FNM_CASEFOLD */) {
		return tolower((unsigned char)a) == tolower((unsigned char)b);
	}
	return a == b;
}

static int st_fnmatch_match(const char *pattern, const char *text, int flags)
{
	for (;;) {
		char const pc = *pattern;
		char const tc = *text;

		if (pc == '\0') {
			return tc == '\0';
		}
		if (pc == '*') {
			do {
				++pattern;
			} while (*pattern == '*');
			if (*pattern == '\0') {
				return 1;
			}
			for (; *text != '\0'; ++text) {
				if (st_fnmatch_match(pattern, text, flags)) {
					return 1;
				}
			}
			return 0;
		}
		if (tc == '\0') {
			return 0;
		}
		if (pc == '?') {
			++pattern;
			++text;
			continue;
		}
		if (!st_fnmatch_char_eq(pc, tc, flags)) {
			return 0;
		}
		++pattern;
		++text;
	}
}

int fnmatch(const char *pattern, const char *name, int flags)
{
	if (!pattern || !name) {
		return 1;
	}
	return st_fnmatch_match(pattern, name, flags) ? 0 : 1;
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
	unsigned long const *hz200 = (volatile unsigned long *)0x4BA;
	unsigned long start, now, ticks, wait_ticks;
	unsigned long long total_ns;

	if (!req) {
		errno = EINVAL;
		return -1;
	}
	if (req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L) {
		errno = EINVAL;
		return -1;
	}

	total_ns = (unsigned long long)req->tv_sec * 1000000000ULL + (unsigned long long)req->tv_nsec;
	wait_ticks = (unsigned long)((total_ns + 4999999ULL) / 5000000ULL);
	if (wait_ticks == 0UL && total_ns != 0ULL) {
		wait_ticks = 1UL;
	}

	start = *hz200;
	do {
		now = *hz200;
		ticks = now - start;
	} while (ticks < wait_ticks);

	if (rem) {
		rem->tv_sec = 0;
		rem->tv_nsec = 0;
	}
	return 0;
}

int mkdir(const char *path, mode_t mode)
{
	long rc;
	(void)mode;
	if (!path || !path[0]) {
		errno = EINVAL;
		return -1;
	}
	rc = Dcreate(path);
	if (rc == 0 || rc == -36) { /* TOS: -36 = EACCDN/exists-ish across variants */
		if (rc == 0) {
			return 0;
		}
		errno = EEXIST;
		return -1;
	}
	errno = EIO;
	return -1;
}

char *__xpg_basename(char *path)
{
	char *base;
	if (!path || !path[0]) {
		return (char *)".";
	}
	base = path + strlen(path);
	while (base > path && (base[-1] == '/' || base[-1] == '\\')) {
		--base;
	}
	while (base > path && base[-1] != '/' && base[-1] != '\\') {
		--base;
	}
	return *base ? base : path;
}

char *realpath(const char *path, char *resolved_path)
{
	char cwd[PATH_MAX];
	size_t need;

	if (!path || !resolved_path) {
		errno = EINVAL;
		return NULL;
	}

	if (path[0] == '/') {
		need = strlen(path);
		if (need >= PATH_MAX) {
			errno = ENAMETOOLONG;
			return NULL;
		}
		strcpy(resolved_path, path);
		return resolved_path;
	}

	if (!getcwd(cwd, sizeof(cwd))) {
		return NULL;
	}
	need = strlen(cwd) + 1 + strlen(path);
	if (need >= PATH_MAX) {
		errno = ENAMETOOLONG;
		return NULL;
	}
	strcpy(resolved_path, cwd);
	if (resolved_path[0] && resolved_path[strlen(resolved_path) - 1] != '/') {
		strcat(resolved_path, "/");
	}
	strcat(resolved_path, path);
	return resolved_path;
}

}

#endif /* LIBCMINI */
