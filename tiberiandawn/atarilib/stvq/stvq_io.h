/*
 * stvq_io.h - C callback IO for STVQ streaming (game CCFileClass / CLI FILE*).
 */
#ifndef STVQ_IO_H
#define STVQ_IO_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct StvqIo {
	void *user;
	/*
	 * Read up to n bytes; return count transferred.
	 * Short read means EOF (not a hard error).
	 */
	size_t (*read)(void *user, void *buf, size_t n);
	/* 0 ok, -1 err; whence is SEEK_SET / SEEK_CUR / SEEK_END. */
	int (*seek)(void *user, long off, int whence);
} StvqIo;

#ifdef __cplusplus
}
#endif

#endif /* STVQ_IO_H */
