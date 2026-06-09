/*
 * st_cache.cpp — D-cache push/invalidate for 68040-class CPUs (no BLiTTER snooping).
 */

#include "st_cache.h"

#include <stdint.h>

#if ST_BLIT_CACHE_COHERENCY

static int ST_Cache_Is_Active(void)
{
#if defined(__mc68040__) || defined(__mc68060__) || defined(__mc68030__)
	return 1;
#else
	extern long _MCPU;
	return _MCPU == 30L || _MCPU == 40L || _MCPU == 60L;
#endif
}

void ST_Cache_Push_Range(const void *start, size_t len)
{
	if (!ST_Cache_Is_Active() || !start || len == 0) {
		return;
	}
	uintptr_t a = (uintptr_t)start & ~(uintptr_t)15u;
	uintptr_t end = (uintptr_t)start + len;
	for (; a < end; a += 16u) {
		__asm__ __volatile__("cpushl %%dc,%0" : : "m" (*(const char *)a));
	}
	__asm__ __volatile__("" ::: "memory");
}

void ST_Cache_Invalidate_Range(const void *start, size_t len)
{
	if (!ST_Cache_Is_Active() || !start || len == 0) {
		return;
	}
	uintptr_t a = (uintptr_t)start & ~(uintptr_t)15u;
	uintptr_t end = (uintptr_t)start + len;
	for (; a < end; a += 16u) {
		__asm__ __volatile__("cinvl %%dc,%0" : : "m" (*(const char *)a));
	}
	__asm__ __volatile__("" ::: "memory");
}

#else /* !ST_BLIT_CACHE_COHERENCY */

void ST_Cache_Push_Range(const void *start, size_t len)
{
	(void)start;
	(void)len;
}

void ST_Cache_Invalidate_Range(const void *start, size_t len)
{
	(void)start;
	(void)len;
}

#endif /* ST_BLIT_CACHE_COHERENCY */
