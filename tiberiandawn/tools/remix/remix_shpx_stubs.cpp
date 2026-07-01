/*
 * Stubs and helpers for remix SHPX / keyframe host builds.
 */

#include "function.h"

#include "lcw.h"
#include "xordelta.h"

#include <stdlib.h>

extern "C" unsigned long LCW_Uncompress(void *source, void *dest, unsigned long length)
{
	return (unsigned long)::LCW_Uncompress((void const *)source, dest, (unsigned)length);
}

void Memory_Error_Handler(void)
{
	abort();
}

extern "C" unsigned int Apply_XOR_Delta(char *target, char *delta, unsigned int frame_bytes)
{
	(void)frame_bytes;
	::Apply_XOR_Delta(target, delta);
	return 0;
}
