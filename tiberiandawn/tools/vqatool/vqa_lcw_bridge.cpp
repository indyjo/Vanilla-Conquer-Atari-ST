/*
 * vqa_lcw_bridge.cpp - C-callable wrapper around common/lcw.cpp (C++ linkage).
 */
#include "lcw.h"

extern "C" int vqa_lcw_uncompress(void const *source, void *dest, unsigned length)
{
	return LCW_Uncompress(source, dest, length);
}
