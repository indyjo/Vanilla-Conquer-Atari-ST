/*
 * misc.h - Miscellaneous functions for Atari ST/MiNT
 * 
 * This provides a compatible interface to WIN32LIB/misc.h
 */

#ifndef MISC_H
#define MISC_H

#include "../COMMONLIB/wwstd.h"
#include "windows.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Random number generation */
int IRandom(int minval, int maxval);
unsigned char Random(void);

/* Program exit function */
void Prog_End(const char *why = NULL, bool fatal = false);

/* Wait for vertical blank */
void Wait_Vert_Blank(void);

/* Confine rectangle function */
int Confine_Rect(int *x, int *y, int dw, int dh, int width, int height);

/* Clip rectangle function */
int Clip_Rect(int *x, int *y, int *dw, int *dh, int width, int height);

/* CRC calculation function - matches WIN32LIB signature */
long Calculate_CRC(void *buffer, long length);

/* Build fading table function - matches WIN32LIB signature */
void *Build_Fading_Table(void const *palette, void const *dest, long int color, long int frac);

/* Icon cache functions - stubs for ATARILIB */
void Restore_Cached_Icons(void);
void Invalidate_Cached_Icons(void);

#ifdef __cplusplus
}
#endif

/* External variables */
extern BOOL OverlappedVideoBlits;	// Can video driver blit overlapped regions?
extern int CachedIconsDrawn;
extern int UnCachedIconsDrawn;

/*=========================================================================*/
/* Definition of surface monitor class                                     */
/*                                                                         */
/* This class keeps track of all the graphic buffers we generate in video  */
/* memory so they can be restored after a focus switch.                    */
/*=========================================================================*/

#define	MAX_SURFACES	20

// Forward declaration for LPDIRECTDRAWSURFACE (stub type for Atari ST)
#ifndef LPDIRECTDRAWSURFACE
typedef void* LPDIRECTDRAWSURFACE;
#endif

class SurfaceMonitorClass {

	public:

		SurfaceMonitorClass();

		void	Add_DD_Surface (LPDIRECTDRAWSURFACE);
		void	Remove_DD_Surface (LPDIRECTDRAWSURFACE);
		BOOL	Got_Surface_Already (LPDIRECTDRAWSURFACE);
		void	Restore_Surfaces (void);
		void	Set_Surface_Focus ( BOOL in_focus );
		void	Release(void);

		BOOL	SurfacesRestored;

	private:

		LPDIRECTDRAWSURFACE	Surface[MAX_SURFACES];
		BOOL						InFocus;

};

extern	SurfaceMonitorClass	AllSurfaces;				//List of all direct draw surfaces

/*=========================================================================*/
/* Bit manipulation functions - ported from WIN32LIB/MiscAsm.cpp          */
/*=========================================================================*/
#ifdef __cplusplus
extern "C" {
#endif
int First_True_Bit(void const * array);
int First_False_Bit(void const * array);
int Bound(int original, int min, int max);
#ifdef __cplusplus
}
#endif

/*
 * Small bit helpers are performance‑critical on 680x0; make them inline so
 * callers like BooleanVectorClass can be optimized without function call
 * overhead.
 */
static inline void Set_Bit(void * array, int bit, int value)
{
	if (!array) return;
	if (bit < 0) return;  /* Invalid bit index */

	unsigned char *byte_array = (unsigned char *)array;
	int byte_index = bit >> 3;   /* Divide by 8 (bits per byte) */
	int bit_index  = bit & 0x7;  /* Modulo 8 (0-7) */

	/* Clear the bit first. Caller must ensure array is large enough. */
	unsigned char mask = (unsigned char)~(1U << bit_index);
	byte_array[byte_index] &= mask;

	/* Set the bit if value is non-zero. */
	if (value) {
		mask = (unsigned char)(1U << bit_index);
		byte_array[byte_index] |= mask;
	}
}

static inline int Get_Bit(void const * array, int bit)
{
	if (!array) return 0;
	if (bit < 0) return 0;  /* Invalid bit index */

	unsigned char const *byte_array = (unsigned char const *)array;
	int byte_index = bit >> 3;   /* Divide by 8 (bits per byte) */
	int bit_index  = bit & 0x7;  /* Modulo 8 (0-7) */

	/* Caller must ensure array is large enough. */
	return (byte_array[byte_index] >> bit_index) & 1;
}

#endif /* MISC_H */
