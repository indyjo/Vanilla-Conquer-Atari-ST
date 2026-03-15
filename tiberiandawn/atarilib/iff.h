/*
 * iff.h - IFF file format header stub for Atari ST/MiNT
 */

#ifndef IFF_H
#define IFF_H

#include "gbuffer.h"
#include "memflag.h"

/*=========================================================================*/
/* Iff and Load Picture system defines and enumerations                    */
/*=========================================================================*/

#define MAKE_ID(a,b,c,d) ((long) ((long) d << 24) | ((long) c << 16) | ((long) b <<  8) | (long)(a))

//lint -strong(AJX,PicturePlaneType)
typedef enum {
	BM_AMIGA,	// Bit plane format (8K per bitplane).
	BM_MCGA,		// Byte per pixel format (64K).
	BM_DEFAULT=BM_MCGA	// Default picture format.
} PicturePlaneType;

/*
**	This is the compression type code.  This value is used in the compressed
**	file header to indicate the method of compression used.
*/
//lint -strong(AJX,CompressionType)
typedef enum {
	NOCOMPRESS,		// No compression (raw data).
	LZW12,			// LZW 12 bit codes.
	LZW14,			// LZW 14 bit codes.
	HORIZONTAL,		// Run length encoding (RLE).
	LCW				// Westwood proprietary compression.
} CompressionType;

/*
**	Compressed blocks of data must start with this header structure.
**	Note that disk based compressed files have an additional two
**	leading bytes that indicate the size of the entire file.
**	Layout is packed (8 bytes total); file format is little-endian.
*/
//lint -strong(AJX,CompHeaderType)
#if defined(__GNUC__) || defined(__clang__)
#pragma pack(push, 1)
#endif
typedef struct {
	char	Method;		// Compression method (CompressionType).
	char	pad;			// Reserved pad byte (always 0).
	long	Size;			// Size of the uncompressed data (LE in file).
	short	Skip;			// Number of bytes to skip before data (LE in file).
} CompHeaderType;
#if defined(__GNUC__) || defined(__clang__)
#pragma pack(pop)
#endif

/*=========================================================================*/
/* Function prototypes                                                     */
/*=========================================================================*/

#ifdef __cplusplus
extern "C" {
#endif

/* Uncompress data from one buffer to another */
unsigned long Uncompress_Data(void const *src, void *dst);

/* LCW (Westwood proprietary) decompression */
unsigned long LCW_Uncompress(void *source, void *dest, unsigned long length);

#ifdef __cplusplus
}
#endif

#endif /* IFF_H */

