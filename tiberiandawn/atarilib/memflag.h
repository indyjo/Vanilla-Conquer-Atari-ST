/*
 * memflag.h - Memory flag header for Atari ST/MiNT
 * 
 * This header provides memory-related functions and utilities.
 */

#ifndef MEMFLAG_H
#define MEMFLAG_H

#include <stddef.h>  // For size_t

// Memory Flags
/*
**	Memory allocation flags.  These are the flags that are passed into Alloc
**	in order to control the type of memory allocated.
*/
typedef enum {
	MEM_NORMAL = 0x0000,		// Default memory (normal).
	MEM_NEW	  = 0x0001,		// Called by the operator new and was overloaded.
	MEM_CLEAR  = 0x0002,		// Clear memory before returning.
	MEM_REAL   = 0x0004,		// Clear memory before returning.
	MEM_TEMP   = 0x0008,		// Clear memory before returning.
	MEM_LOCK   = 0x0010,		// Lock the memory that we allocated
} MemoryFlagType;


/*=========================================================================
 * The following prototypes are for the file: MEM_COPY.ASM
 *=========================================================================*/

#ifdef __cplusplus
extern "C" {
#endif

void Mem_Copy(void const *source, void *dest, unsigned long bytes_to_copy);

#ifdef __cplusplus
}
#endif

/*=========================================================================
 * Pointer arithmetic function
 *=========================================================================*/
#ifdef __cplusplus
inline void *Add_Long_To_Pointer(void const *ptr, long size)
{
	return ((void *) ( (char const *) ptr + size));
}

extern void (*Memory_Error)(void);
extern void (*Memory_Error_Exit)(char *string);

extern unsigned long MinRam;		// Record of least memory at worst case.
extern unsigned long MaxRam;		// Record of total allocated at worst case.

/*=========================================================================
 * Memory allocation functions
 *=========================================================================*/
void *Alloc(unsigned long bytes_to_alloc, MemoryFlagType flags);
void Free(void const *pointer);
void *Resize_Alloc(void const *original_ptr, unsigned long new_size_in_bytes);
long Ram_Free(MemoryFlagType flag);
long Heap_Size(MemoryFlagType flag);
long Total_Ram_Free(MemoryFlagType flag);

/* Chip/ST-RAM visible to BLiTTER and STE DMA (not TT-RAM). */
void *Stram_Alloc(unsigned long bytes_to_alloc);
void Stram_Free(void *pointer);

/* Alternate (TT/fast) RAM. Returns NULL on machines that have none. */
void *Ttram_Alloc(unsigned long bytes_to_alloc);
void Ttram_Free(void *pointer);

/*
 * Largest single free block per GEMDOS pool -- what a big allocation must fit
 * into. Either pointer may be NULL; ttram is 0 without alternate RAM.
 */
void ST_Largest_Free_Blocks(long *stram, long *ttram);

/*
 * Log all GEMDOS free ST-RAM / TT-RAM blocks (sizes + total + largest).
 * label may be NULL. Ram_Free() also dumps the ST-RAM block list when called.
 * Diagnostic only — Alloc uses the C heap, which may not match these figures.
 */
void ST_Log_Free_Memory(const char *label);

/*=========================================================================
 * Operator new overloads for memory flags
 *=========================================================================*/
void * operator new(size_t size, MemoryFlagType flag);
void * operator new[] (size_t size, MemoryFlagType flag);

inline void * operator new(size_t size, MemoryFlagType flag)
{
	return(Alloc(size, flag));
}
inline void * operator new[] (size_t size, MemoryFlagType flag)
{
	return(Alloc(size, flag));
}

#endif

#endif /* MEMFLAG_H */


