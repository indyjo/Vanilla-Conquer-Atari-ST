/*
 * st_cache.cpp — Function pointers default to no-ops; ST_Cache_Init binds 68040+ impls.
 */

#include "st_cache.h"

#include "st_cache_ops.h"

#include <mint/cookie.h>

static void ST_Cache_Push_Range_Noop(const void *start, size_t len)
{
	(void)start;
	(void)len;
}

static void ST_Cache_Invalidate_Range_Noop(const void *start, size_t len)
{
	(void)start;
	(void)len;
}

#if ST_BLIT_CACHE_COHERENCY

static void ST_Cache_Push_Range_Impl(const void *start, size_t len)
{
	if (!start || len == 0) {
		return;
	}
	ST_Cache_Push_Lines(start, len);
}

static void ST_Cache_Invalidate_Range_Impl(const void *start, size_t len)
{
	if (!start || len == 0) {
		return;
	}
	ST_Cache_Invalidate_Lines(start, len);
}

#endif /* ST_BLIT_CACHE_COHERENCY */

ST_Cache_Range_Fn ST_Cache_Push_Range = ST_Cache_Push_Range_Noop;
ST_Cache_Range_Fn ST_Cache_Invalidate_Range = ST_Cache_Invalidate_Range_Noop;

void ST_Cache_Init(void)
{
#if ST_BLIT_CACHE_COHERENCY
	long cpu = 0;

	if (Getcookie(C__CPU, &cpu) != C_FOUND) {
		return;
	}

	cpu &= 0xFFFFL;
	if (cpu == 40L || cpu == 60L) {
		ST_Cache_Push_Range = ST_Cache_Push_Range_Impl;
		ST_Cache_Invalidate_Range = ST_Cache_Invalidate_Range_Impl;
	}
#endif
}
