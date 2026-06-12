/*
 * misc.h - Miscellaneous functions for Atari ST/MiNT
 * 
 * This provides a compatible interface to WIN32LIB/misc.h
 */

#ifndef ATARILIB_MISC_H
#define ATARILIB_MISC_H

#include "../COMMONLIB/wwstd.h"
#include "windows.h"

#ifdef __cplusplus
extern "C" {
#endif

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

/* True from program start until Init_Game completes successfully. */
bool ST_Game_Still_Initializing(void);
void ST_Mark_Game_Init_Complete(void);

/* During initialization, wait for Enter on the console before exiting. */
void ST_Init_Await_Keypress(void);

#ifdef __cplusplus
}
#endif

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

/*
 * Bit helpers live in bitintrin.h for BooleanVectorClass.
 */

#endif /* ATARILIB_MISC_H */
