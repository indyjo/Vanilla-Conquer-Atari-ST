/*
 * buffer.h - Buffer class header stub for Atari ST/MiNT
 */

#ifndef BUFFER_H
#define BUFFER_H

#include "windows.h"  // For BOOL type

// Minimal BufferClass definition for use in main source
class BufferClass {
public:
	void *Get_Buffer(void) { return Buffer; }
	long Get_Size(void) { return Size; }

protected:
	void *Buffer;
	long Size;
	BOOL Allocated;
};

#endif /* BUFFER_H */

