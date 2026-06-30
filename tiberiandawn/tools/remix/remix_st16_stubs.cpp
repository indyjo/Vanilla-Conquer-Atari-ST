/*
 * Minimal Alloc/Free and C2P cache hooks for remix ST16 host/WASM builds.
 */

#include "memflag.h"

#include <stdlib.h>
#include <string.h>

extern "C" void Mem_Copy(void const *source, void *dest, unsigned long bytes_to_copy)
{
	memcpy(dest, source, bytes_to_copy);
}

void (*Memory_Error)(void) = NULL;
void (*Memory_Error_Exit)(char *string) = NULL;
unsigned long MinRam = 0;
unsigned long MaxRam = 0;

void *Alloc(unsigned long bytes_to_alloc, MemoryFlagType flags)
{
	void *p = malloc(bytes_to_alloc);
	if (!p) {
		return NULL;
	}
	if (flags & MEM_CLEAR) {
		memset(p, 0, bytes_to_alloc);
	}
	return p;
}

void Free(void const *pointer)
{
	if (pointer) {
		free((void *)pointer);
	}
}

void *Resize_Alloc(void const *original_ptr, unsigned long new_size_in_bytes)
{
	if (!original_ptr) {
		return Alloc(new_size_in_bytes, MEM_NORMAL);
	}
	return realloc((void *)original_ptr, new_size_in_bytes);
}

long Ram_Free(MemoryFlagType flag)
{
	(void)flag;
	return 0;
}

long Heap_Size(MemoryFlagType flag)
{
	(void)flag;
	return 0;
}

long Total_Ram_Free(MemoryFlagType flag)
{
	(void)flag;
	return 0;
}

void *Stram_Alloc(unsigned long bytes_to_alloc)
{
	return Alloc(bytes_to_alloc, MEM_NORMAL);
}

void Stram_Free(void *pointer)
{
	Free(pointer);
}

void ST_Log_Free_Memory(const char *label)
{
	(void)label;
}

extern "C" void Invalidate_Mouse_Planar_Cache(void)
{
}

extern "C" void ST_SPRITE_CACHE_Invalidate_Planar_Cache(void)
{
}
