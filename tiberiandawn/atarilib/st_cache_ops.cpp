/*
 * st_cache_ops.cpp — 68030/68040/68060 cache line push/invalidate (cpushl/cinvl).
 * Compiled with -mcpu=68040 so the assembler accepts these opcodes. Only called
 * from st_cache.cpp after a runtime _CPU cookie probe.
 */

#include "st_cache_ops.h"

#include <stdint.h>

void ST_Cache_Push_Lines(const void *start, size_t len)
{
	if (!start || len == 0) {
		return;
	}
	uintptr_t a = (uintptr_t)start & ~(uintptr_t)15u;
	uintptr_t end = (uintptr_t)start + len;
	for (; a < end; a += 16u) {
		__asm__ __volatile__("cpushl %%dc,%0" : : "m" (*(const char *)a));
	}
	__asm__ __volatile__("" ::: "memory");
}

void ST_Cache_Invalidate_Lines(const void *start, size_t len)
{
	if (!start || len == 0) {
		return;
	}
	uintptr_t a = (uintptr_t)start & ~(uintptr_t)15u;
	uintptr_t end = (uintptr_t)start + len;
	for (; a < end; a += 16u) {
		__asm__ __volatile__("cinvl %%dc,%0" : : "m" (*(const char *)a));
	}
	__asm__ __volatile__("" ::: "memory");
}
