/*
 * wsa.h - Windows Sockets API header stub for Atari ST/MiNT
 */

#ifndef WSA_H
#define WSA_H

#ifdef __cplusplus
extern "C" {
#endif

/*=========================================================================*/
/* XOR Delta functions - for applying delta compression to buffers         */
/*=========================================================================*/

/* Apply XOR delta data to a linear buffer */
unsigned int Apply_XOR_Delta(char *target, char *delta);

/* Apply XOR delta to a page or viewport */
void Apply_XOR_Delta_To_Page_Or_Viewport(void *target, void *delta, int width, int nextrow, int copy);

#ifdef __cplusplus
}
#endif

#endif /* WSA_H */

