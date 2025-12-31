/*
 * wwmem.h - Memory management header for Atari ST/MiNT
 */

#ifndef WWMEM_H
#define WWMEM_H

#include "../COMMONLIB/wwstd.h"
#include "memflag.h"

/*=========================================================================
 * The following prototypes are for the file: MEM.CPP
 *=========================================================================*/

int  Mem_Init(void *buffer, long size);
void *Mem_Alloc(void *poolptr, long lsize, unsigned long id);
int  Mem_Free(void *poolptr, void *buffer);
void Mem_Reference(void *node);
void Mem_Lock_Block(void *node);
void Mem_In_Use(void *node);
void *Mem_Find(void *poolptr, unsigned long id);
unsigned long Mem_Get_ID(void *node);
void *Mem_Find_Oldest(void *poolptr);
void *Mem_Free_Oldest(void *poolptr);
long Mem_Pool_Size(void *poolptr);
long Mem_Avail(void *poolptr);
long Mem_Largest_Avail(void *poolptr);
void Mem_Cleanup(void *poolptr);

#endif /* WWMEM_H */

