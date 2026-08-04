/*
 * alloc.cpp - Memory allocation functions for Atari ST/MiNT
 *
 * Alloc/Free/Resize_Alloc use libcmini malloc (one GEMDOS block for the C
 * heap). Direct GEMDOS Mxalloc/Malloc is reserved for Stram_Alloc when the
 * BLiTTER / STE DMA / shifter need chip RAM.
 */

#include "memflag.h"
#include "debugstring.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mint/osbind.h>
#include <mint/ostruct.h>

/*=========================================================================*/
/* Mem_Copy -- Copies memory from source to destination                   */
/*=========================================================================*/
extern "C" void Mem_Copy(void const *source, void *dest, unsigned long bytes_to_copy)
{
	memcpy(dest, source, bytes_to_copy);
}

// External variables
void (*Memory_Error)(void) = NULL;
void (*Memory_Error_Exit)(char *string) = NULL;
unsigned long MinRam = 0;
unsigned long MaxRam = 0;

/*=========================================================================*/
/* Alloc -- Allocates system RAM (libc heap)                               */
/*=========================================================================*/
void *Alloc(unsigned long bytes_to_alloc, MemoryFlagType flags)
{
	void *retval = malloc(bytes_to_alloc);

	if (retval == NULL) {
		DBG_ERROR("Alloc failed: %lu bytes (~%lu KiB)",
		    bytes_to_alloc,
		    bytes_to_alloc / 1024UL);
		ST_Log_Free_Memory("Alloc failed");
		if (Memory_Error != NULL) {
			Memory_Error();
		}
		return NULL;
	}

	if (flags & MEM_CLEAR) {
		memset(retval, 0, bytes_to_alloc);
	}

	return retval;
}

/*=========================================================================*/
/* Free -- Free an Alloc'ed block of RAM                                   */
/*=========================================================================*/
void Free(void const *pointer)
{
	if (pointer) {
		free((void *)pointer);
	}
}

/*=========================================================================*/
/* Resize_Alloc -- Change the size of an allocated block                  */
/*=========================================================================*/
void *Resize_Alloc(void const *original_ptr, unsigned long new_size_in_bytes)
{
	void *retval;

	if (original_ptr == NULL) {
		return Alloc(new_size_in_bytes, MEM_NORMAL);
	}

	retval = realloc((void *)original_ptr, new_size_in_bytes);
	if (retval == NULL) {
		DBG_ERROR("Resize_Alloc failed: %lu bytes (~%lu KiB)",
		    new_size_in_bytes,
		    new_size_in_bytes / 1024UL);
		ST_Log_Free_Memory("Resize_Alloc failed");
		if (Memory_Error != NULL) {
			Memory_Error();
		}
	}

	return retval;
}

/*
 * Mxalloc (GEMDOS 0x44) needs GEMDOS >= 0.19 (Sversion >= 0x1900).
 * TOS 1.0–1.6x ship GEMDOS 0.13–0.17 — trap returns EINVFN; treat as no Mxalloc.
 * On those systems Malloc is always ST-RAM (no TT alternate RAM).
 */
static int gemdos_has_mxalloc(void)
{
	static int cached = -1;

	if (cached < 0)
		cached = (Sversion() >= 0x1900) ? 1 : 0;
	return cached;
}

void *Stram_Alloc(unsigned long bytes_to_alloc)
{
	long a;

	if (gemdos_has_mxalloc())
		a = Mxalloc((long)bytes_to_alloc, MX_STRAM);
	else
		a = Malloc((long)bytes_to_alloc);
	return a > 0L ? (void *)a : (void *)0;
}

void Stram_Free(void *pointer)
{
	if (pointer) {
		Mfree(pointer);
	}
}

typedef struct StFreePoolStats {
	long largest;
	long total;
	int nblocks;
} StFreePoolStats;

/*
 * Enumerate GEMDOS free blocks by claiming each largest chunk then releasing.
 * Holds every fragment until the walk finishes so adjacent blocks do not merge
 * mid-pass. Logs each block (and totals) when do_log != 0.
 *
 * Diagnostic only — game Alloc uses the C heap, so these figures can differ
 * from what malloc can still satisfy.
 */
static void walk_gemdos_free_pool(int is_tt, int do_log, const char *pool_name, StFreePoolStats *out)
{
	enum { MAX_BLOCKS = 96 };
	void *hold[MAX_BLOCKS];
	long sizes[MAX_BLOCKS];
	int n = 0;
	long largest = 0L;
	long total = 0L;
	int i;

	out->largest = 0L;
	out->total = 0L;
	out->nblocks = 0;

	if (is_tt && !gemdos_has_mxalloc()) {
		if (do_log) {
			DBG_INFO("C&C ST - %s free: (no Mxalloc / TT-RAM)", pool_name);
		}
		return;
	}

	for (;;) {
		long sz;
		long a;
		void *p;

		if (is_tt) {
			sz = Mxalloc(-1L, MX_TTRAM);
		} else if (gemdos_has_mxalloc()) {
			sz = Mxalloc(-1L, MX_STRAM);
		} else {
			sz = Malloc(-1L);
		}
		if (sz <= 0L) {
			break;
		}

		if (is_tt) {
			a = Mxalloc(sz, MX_TTRAM);
		} else if (gemdos_has_mxalloc()) {
			a = Mxalloc(sz, MX_STRAM);
		} else {
			a = Malloc(sz);
		}
		if (a <= 0L) {
			break;
		}
		p = (void *)a;

		if (n >= MAX_BLOCKS) {
			Mfree(p);
			if (do_log) {
				DBG_INFO("C&C ST - %s free: truncated after %d blocks", pool_name, MAX_BLOCKS);
			}
			break;
		}

		hold[n] = p;
		sizes[n] = sz;
		total += sz;
		if (sz > largest) {
			largest = sz;
		}
		n++;
	}

	if (do_log) {
		DBG_INFO("C&C ST - %s free: %d block(s), total %ld b (~%ld KiB), largest %ld b (~%ld KiB)",
		    pool_name,
		    n,
		    total,
		    (total > 0L) ? (total / 1024L) : 0L,
		    largest,
		    (largest > 0L) ? (largest / 1024L) : 0L);
		for (i = 0; i < n; i++) {
			DBG_INFO("C&C ST - %s free[%d]: %ld b (~%ld KiB) @ %p",
			    pool_name,
			    i,
			    sizes[i],
			    (sizes[i] > 0L) ? (sizes[i] / 1024L) : 0L,
			    hold[i]);
		}
	}

	for (i = 0; i < n; i++) {
		Mfree(hold[i]);
	}

	out->largest = largest;
	out->total = total;
	out->nblocks = n;
}

/*=========================================================================*/
/* Ram_Free -- Determines the largest free chunk of RAM                    */
/*=========================================================================*/
long Ram_Free(MemoryFlagType flag)
{
	StFreePoolStats st;

	(void)flag;
	walk_gemdos_free_pool(0, 1, "ST-RAM", &st);
	return st.largest;
}

/*=========================================================================*/
/* Heap_Size -- Size of the heap we have                                   */
/*=========================================================================*/
long Heap_Size(MemoryFlagType flag)
{
	(void)flag;
	return 0;
}

/*=========================================================================*/
/* Total_Ram_Free -- Total amount of free RAM                              */
/*=========================================================================*/
long Total_Ram_Free(MemoryFlagType flag)
{
	StFreePoolStats st;
	StFreePoolStats tt;

	(void)flag;
	walk_gemdos_free_pool(0, 0, "ST-RAM", &st);
	walk_gemdos_free_pool(1, 0, "TT-RAM", &tt);
	return st.total + tt.total;
}

void ST_Log_Free_Memory(const char *label)
{
	StFreePoolStats st;
	StFreePoolStats tt;
	const char *tag = (label != NULL && label[0] != '\0') ? label : "free memory";

	DBG_INFO("C&C ST - %s:", tag);
	walk_gemdos_free_pool(0, 1, "ST-RAM", &st);
	walk_gemdos_free_pool(1, 1, "TT-RAM", &tt);
	DBG_INFO("C&C ST - %s summary: ST total %ld b (~%ld KiB) largest %ld b, TT total %ld b (~%ld KiB) largest %ld b, grand total %ld b (~%ld KiB)",
	    tag,
	    st.total,
	    (st.total > 0L) ? (st.total / 1024L) : 0L,
	    st.largest,
	    tt.total,
	    (tt.total > 0L) ? (tt.total / 1024L) : 0L,
	    tt.largest,
	    st.total + tt.total,
	    ((st.total + tt.total) > 0L) ? ((st.total + tt.total) / 1024L) : 0L);
}
