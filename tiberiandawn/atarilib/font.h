/*
 * font.h - Font header for Atari ST/MiNT
 * 
 * This provides a compatible interface to WIN32LIB/font.h
 */

#ifndef FONT_H
#define FONT_H

#ifndef GBUFFER_H
#include "gbuffer.h"
#endif

//////////////////////////////////////// Defines //////////////////////////////////////////

// defines for font header, offsets to block offsets

#define FONTINFOBLOCK			4
#define FONTOFFSETBLOCK			6
#define FONTWIDTHBLOCK			8
#define FONTDATABLOCK			10
#define FONTHEIGHTBLOCK			12

// defines for font info block

#define FONTINFOMAXHEIGHT		4
#define FONTINFOMAXWIDTH		5

//////////////////////////////////////// Prototypes //////////////////////////////////////////

/*=========================================================================*/
/* The following prototypes are for the file: SET_FONT.CPP				*/
/*=========================================================================*/

void  * Set_Font(void const *fontptr);

/*=========================================================================*/
/* The following prototypes are for the file: FONT.CPP						*/
/*=========================================================================*/

int Char_Pixel_Width(char chr);
unsigned int String_Pixel_Width(char const *string);
void Get_Next_Text_Print_XY(GraphicViewPortClass& vp, unsigned long offset, int *x, int *y);

/*=========================================================================*/
/* The following prototypes are for the file: LOADFONT.CPP					*/
/*=========================================================================*/

void * Load_Font(char  const *name);

/*=========================================================================*/
/* The following prototypes are for the file: TEXTPRNT.ASM					*/
/*=========================================================================*/

#ifdef __cplusplus
extern "C" {
#endif

void Set_Font_Palette_Range(void const *palette, int start_idx, int end_idx);

#ifdef __cplusplus
}
#endif

/*=========================================================================*/

//////////////////////////////////////// External variables ///////////////////////////////////////
extern "C" int FontXSpacing;
extern "C" int FontYSpacing;
extern char FontWidth;
extern char FontHeight;
extern char *FontWidthBlockPtr;

extern "C" void const *FontPtr;

#endif /* FONT_H */

