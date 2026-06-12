/*
 * alloc.cpp - Memory allocation functions for Atari ST/MiNT
 * 
 * This provides portable implementations of Alloc, Free, and Resize_Alloc
 */

#include "memflag.h"
#include <stdlib.h>
#include <string.h>

#include <mint/osbind.h>
#include <mint/ostruct.h>

/*=========================================================================*/
/* Mem_Copy -- Copies memory from source to destination                   */
/*                                                                         */
/* INPUT:                                                                  */
/*   source         -- Source memory pointer                               */
/*   dest           -- Destination memory pointer                        */
/*   bytes_to_copy  -- Number of bytes to copy                            */
/*                                                                         */
/* OUTPUT:                                                                 */
/*   none                                                                  */
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
/* Alloc -- Allocates system RAM                                           */
/*                                                                         */
/* INPUT:                                                                  */
/*   bytes_to_alloc -- Number of bytes to allocate                        */
/*   flags          -- Memory allocation control flags                    */
/*                                                                         */
/* OUTPUT:                                                                 */
/*   Returns pointer to allocated block, or NULL on failure               */
/*=========================================================================*/
void *Alloc(unsigned long bytes_to_alloc, MemoryFlagType flags)
{
	void *retval = NULL;
	
	// Allocate memory using standard malloc
	retval = malloc(bytes_to_alloc);
	
	// If allocation failed, call error handler
	if (retval == NULL) {
		if (Memory_Error != NULL) {
			Memory_Error();
		}
		return NULL;
	}
	
	// Clear memory if MEM_CLEAR flag is set
	if (flags & MEM_CLEAR) {
		memset(retval, 0, bytes_to_alloc);
	}
	
	return retval;
}

/*=========================================================================*/
/* Free -- Free an Alloc'ed block of RAM                                   */
/*                                                                         */
/* INPUT:                                                                  */
/*   pointer -- Pointer to block of RAM from Alloc                         */
/*                                                                         */
/* OUTPUT:                                                                 */
/*   None                                                                  */
/*=========================================================================*/
void Free(void const *pointer)
{
	if (pointer) {
		free((void *)pointer);
	}
}

/*=========================================================================*/
/* Resize_Alloc -- Change the size of an allocated block                  */
/*                                                                         */
/* INPUT:                                                                  */
/*   original_ptr      -- Pointer to previously allocated block           */
/*   new_size_in_bytes -- New size in bytes                               */
/*                                                                         */
/* OUTPUT:                                                                 */
/*   Returns pointer to resized block, or NULL on failure                 */
/*=========================================================================*/
void *Resize_Alloc(void const *original_ptr, unsigned long new_size_in_bytes)
{
	void *retval = NULL;
	
	if (original_ptr == NULL) {
		// If original pointer is NULL, just allocate new block
		return Alloc(new_size_in_bytes, MEM_NORMAL);
	}
	
	// Use realloc to resize the block
	retval = realloc((void *)original_ptr, new_size_in_bytes);
	
	// If reallocation failed, call error handler
	if (retval == NULL) {
		if (Memory_Error != NULL) {
			Memory_Error();
		}
	}
	
	return retval;
}

/*=========================================================================*/
/* Ram_Free -- Determines the largest free chunk of RAM                    */
/*=========================================================================*/
long Ram_Free(MemoryFlagType flag)
{
	(void)flag;
	/*
	 * MiNT C library: size argument -1 returns the largest free block for this RAM type.
	 * Game allocations expect ST-RAM (chip/blitter-visible); TT-RAM is separate.
	 */
	long const n = Mxalloc(-1L, MX_STRAM);
	return (n > 0L) ? n : 0L;
}

/*=========================================================================*/
/* Heap_Size -- Size of the heap we have                                   */
/*=========================================================================*/
long Heap_Size(MemoryFlagType flag)
{
	// Stub implementation
	(void)flag; // Unused parameter
	return 0;
}

/*=========================================================================*/
/* Total_Ram_Free -- Total amount of free RAM                              */
/*=========================================================================*/
long Total_Ram_Free(MemoryFlagType flag)
{
	(void)flag;
	/*
	 * No single Mxalloc call sums all fragments; report largest ST block plus largest TT block
	 * (two pools — not one contiguous region).
	 */
	long st = Mxalloc(-1L, MX_STRAM);
	long tt = Mxalloc(-1L, MX_TTRAM);
	if (st < 0L)
		st = 0L;
	if (tt < 0L)
		tt = 0L;
	return st + tt;
}

void *Stram_Alloc(unsigned long bytes_to_alloc)
{
	long const a = Mxalloc((long)bytes_to_alloc, MX_STRAM);
	return a > 0L ? (void *)a : (void *)0;
}

void Stram_Free(void *pointer)
{
	if (pointer) {
		Mfree(pointer);
	}
}

