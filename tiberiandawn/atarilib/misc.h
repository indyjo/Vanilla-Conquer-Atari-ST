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

#endif /* MISC_H */
