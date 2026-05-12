/*
 * wsa.h - WSA (Westwood Animation) and XOR Delta functions for Atari ST/MiNT
 */

#ifndef WSA_H
#define WSA_H

#include "gbuffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=========================================================================*/
/* WSA Animation Types                                                    */
/*=========================================================================*/

//lint -strong(AJX,WSAType)
typedef enum {
	WSA_NORMAL,								// Normal WSA animation
	WSA_GHOST	 	= 0x1000,			// Or'd with the above flags to get ghosting
	WSA_PRIORITY2 	= 0x2000,			// Copy using a priority (or in the priority)
	WSA_TRANS    	= 0x4000,			// Copy frame, ignoring transparent colors
	WSA_PRIORITY 	= 0x8000				// Copy using a priority (or in the priority)
} WSAType;

//lint -strong(AJX,WSAOpenType)
typedef enum {
	WSA_OPEN_FROM_MEM		= 0x0000,	// Try to load entire anim into memory.
	WSA_OPEN_INDIRECT		= 0x0000,	// First animate to internal buffer, then copy to page/viewport.
	WSA_OPEN_FROM_DISK	= 0x0001,	// Force the animation to be disk based.
	WSA_OPEN_DIRECT		= 0x0002,	// Animate directly to page or viewport.
	WSA_OPEN_TO_PAGE  = WSA_OPEN_DIRECT,
	WSA_OPEN_TO_BUFFER= WSA_OPEN_INDIRECT,
	/* Atari ST: skip WSA_Atari_TryInstallC2PWeights in Open_Animation; call Install_Animation_C2P_WeightSet before first C2P blit. */
	WSA_DEFERRED_C2P_WEIGHTSET = 0x0004,
} WSAOpenType;

/*=========================================================================*/
/* WSA Animation Functions                                                 */
/*=========================================================================*/

void *Open_Animation(char const *file_name, char *user_buffer, long user_buffer_size, WSAOpenType user_flags, unsigned char *palette=NULL);
void Close_Animation(void *handle);
#ifdef ATARI_ST
void Install_Animation_C2P_WeightSet(void *handle);
#endif
BOOL Animate_Frame(void *handle, GraphicViewPortClass& view, int frame_number, int x_pixel=0, int y_pixel=0, WSAType flags_and_prio=WSA_NORMAL, void *magic_cols=NULL, void *magic=NULL);
int Get_Animation_Frame_Count(void *handle);
int Get_Animation_X(void const *handle);
int Get_Animation_Y(void const *handle);
int Get_Animation_Width(void const *handle);
int Get_Animation_Height(void const *handle);
int Get_Animation_Palette(void const *handle);
unsigned long Get_Animation_Size(void const *handle);

/*=========================================================================*/
/* XOR Delta functions - for applying delta compression to buffers         */
/*=========================================================================*/

/* Apply XOR delta data to a linear buffer. frame_bytes==0 skips target bounds checks;
 * otherwise DEBUG builds assert that skips/XORs stay within [target, target+frame_bytes). */
unsigned int Apply_XOR_Delta(char *target, char *delta, unsigned int frame_bytes);

/* Apply XOR delta to a page or viewport */
void Apply_XOR_Delta_To_Page_Or_Viewport(void *target, void *delta, int width, int nextrow, int copy);

#ifdef __cplusplus
}
#endif

#endif /* WSA_H */

