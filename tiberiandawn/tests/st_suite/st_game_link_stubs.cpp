/*
 * Minimal globals / hooks so CCFileClass + MixFileClass link in cnc_st_tests.tos
 * without pulling in CONQUER.CPP / INIT.CPP / JSHELL.CPP.
 *
 * _ShapeBuffer / _ShapeBufferSize: INIT sets these for getshape.cpp; st-tests leave
 * them NULL/0 so Decode_Shape_To_Buffer allocates scratch (see ATARILIB/getshape.cpp).
 */

#include "function.h"
#include "gbuffer.h"
#include "audio.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* KEYFRAME.CPP references these; full game defines them in GLOBALS / WINSTUB. */
long Frame = 0;

void Memory_Error_Handler(void)
{
}

extern "C" {
char *_ShapeBuffer = NULL;
long _ShapeBufferSize = 0;
}

bool RunningAsDLL = false;
int RequiredCD = -2;
unsigned char *GamePalette = NULL;

/* Defined in AUDIO.CPP for full game; audio_ste.cpp expects these for SampleType writes. */
Sample_Type SampleType = SAMPLE_NONE;
SFX_Type SoundType = SFX_NONE;

bool Force_CD_Available(int)
{
	return true;
}

void Prog_End(const char *, bool)
{
}

void Fatal(char const *message, ...)
{
	va_list va;
	va_start(va, message);
	vfprintf(stderr, message, va);
	va_end(va);
	fputc('\n', stderr);
	if (!RunningAsDLL)
		exit(EXIT_FAILURE);
}

int Get_CD_Index(int, int)
{
	return -1;
}

void *Load_Alloc_Data(FileClass &file)
{
	void *ptr = NULL;
	long size = 0;
	long got = 0;
	const char *filename = file.File_Name() ? file.File_Name() : "(unknown)";

	if (!file.Is_Available()) {
		fprintf(stderr, "Load_Alloc_Data: not found: %s\n", filename);
		exit(EXIT_FAILURE);
	}
	if (!file.Is_Open()) {
		file.Open(READ);
		if (!file.Is_Open()) {
			fprintf(stderr, "Load_Alloc_Data: open failed: %s\n", filename);
			exit(EXIT_FAILURE);
		}
	}
	size = file.Size();
	if (size <= 0) {
		file.Close();
		fprintf(stderr, "Load_Alloc_Data: bad size: %s\n", filename);
		exit(EXIT_FAILURE);
	}
	ptr = new char[(size_t)size];
	if (!ptr) {
		file.Close();
		fprintf(stderr, "Load_Alloc_Data: oom: %s\n", filename);
		exit(EXIT_FAILURE);
	}
	/*
	 * RawFile/CCFile size queries and prior users can leave file position away from
	 * start; always rewind before bulk read, and fail hard on short reads so callers
	 * never consume partially initialized blobs.
	 */
	file.Seek(0, SEEK_SET);
	memset(ptr, 0, (size_t)size);
	got = file.Read(ptr, size);
	if (got != size) {
		file.Close();
		delete[] (char *)ptr;
		fprintf(stderr, "Load_Alloc_Data: short read: %s got=%ld want=%ld\n", filename, got, size);
		exit(EXIT_FAILURE);
	}
	file.Close();
	return ptr;
}

/*
 * drawbuff.cpp references Buffer_Frame_To_Page (full implementation in WINSTUB.CPP);
 * st-tests do not link WINSTUB — provide a no-op so the suite links.
 */
long Buffer_Frame_To_Page(int x, int y, int w, int h, void *Buffer, GraphicViewPortClass &view, int flags, ...)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	(void)Buffer;
	(void)view;
	(void)flags;
	return 0;
}
