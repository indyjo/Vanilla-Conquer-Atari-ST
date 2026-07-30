//
// Copyright 2020 Electronic Arts Inc.
//
// TiberianDawn.DLL and RedAlert.dll and corresponding source code is free 
// software: you can redistribute it and/or modify it under the terms of 
// the GNU General Public License as published by the Free Software Foundation, 
// either version 3 of the License, or (at your option) any later version.

// TiberianDawn.DLL and RedAlert.dll and corresponding source code is distributed 
// in the hope that it will be useful, but with permitted additional restrictions 
// under Section 7 of the GPL. See the GNU General Public License in LICENSE.TXT 
// distributed with this program. You should have received a copy of the 
// GNU General Public License along with permitted additional restrictions 
// with this program. If not, see https://github.com/electronicarts/CnC_Remastered_Collection

/* $Header:   F:\projects\c&c\vcs\code\keyframe.cpv   2.14   16 Oct 1995 16:48:54   JOE_BOSTIC  $ */
/***********************************************************************************************
 ***             C O N F I D E N T I A L  ---  W E S T W O O D   S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                    File Name : KEYFRAME.CPP                                                 *
 *                                                                                             *
 *                   Programmer : Joe L. Bostic                                                *
 *                                                                                             *
 *                   Start Date : 06/25/95                                                     *
 *                                                                                             *
 *                  Last Update : June 25, 1995 [JLB]                                          *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   Get_Build_Frame_Count -- Fetches the number of frames in data block.                      *
 *   Get_Build_Frame_Width -- Fetches the width of the shape image.                            *
 *   Get_Build_Frame_Height -- Fetches the height of the shape image.                          *
 *   Get_Build_Frame_BufferBytes -- Min bytes for Build_Frame output buffer (wh vs largest).   *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#include "function.h"
#ifdef ATARI_ST
#include "atarilib/shpx.h"
#include <string.h>
#endif
extern "C" unsigned long LCW_Uncompress(void *source, void *dest, unsigned long length);
#ifdef DEBUG
#include <stdio.h>
#endif

#define SUBFRAMEOFFS			7	// 3 1/2 frame offsets loaded (2 offsets/frame)
/* Optional: with DEBUG, also define BUILD_FRAME_XOR_TRACE for per-frame XOR printf spam. */


#define	Apply_Delta(buffer, delta, bufsize)	\
	Apply_XOR_Delta((char*)(buffer), (char*)(delta), (unsigned int)(bufsize))

typedef struct {
	unsigned short frames;
	unsigned short x;
	unsigned short y;
	unsigned short width;
	unsigned short height;
	unsigned short largest_frame_size;
	short				flags;
} KeyFrameHeaderType;

#define	INITIAL_BIG_SHAPE_BUFFER_SIZE	12000*1024
#define	THEATER_BIG_SHAPE_BUFFER_SIZE 1000*1024
#define	UNCOMPRESS_MAGIC_NUMBER			56789

unsigned	BigShapeBufferLength = INITIAL_BIG_SHAPE_BUFFER_SIZE;
unsigned	TheaterShapeBufferLength = THEATER_BIG_SHAPE_BUFFER_SIZE;
char *BigShapeBufferStart = NULL;
char *TheaterShapeBufferStart = NULL;
unsigned int UseBigShapeBuffer = 0;
unsigned int IsTheaterShape = 0;
char		*BigShapeBufferPtr = NULL;
int			TotalBigShapes=0;
BOOL		ReallocShapeBufferFlag = FALSE;
bool		OriginalUseBigShapeBuffer = false;

char		*TheaterShapeBufferPtr = NULL;
int			TotalTheaterShapes = 0;



#define MAX_SLOTS 1500
#define THEATER_SLOT_START 1000

char	**KeyFrameSlots [MAX_SLOTS];
int 	TotalSlotsUsed=0;
int		TheaterSlotsUsed = THEATER_SLOT_START;


typedef struct tShapeHeaderType{
	unsigned draw_flags;
	char		*shape_data;
	int		shape_buffer;		//1 if shape is in theater buffer
} ShapeHeaderType;

static int Length;

// Helper functions to read little-endian values from file data
static inline unsigned short ReadLE16(const unsigned char* ptr) {
	return ptr[0] | (ptr[1] << 8);
}

static inline unsigned long ReadLE32(const unsigned char* ptr) {
	return ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);
}

#ifdef ATARI_ST
/*
 * Per-call decode context for Build_Frame on Atari ST.
 *
 * Monolithic KeyFrame blobs keep frame tables and LCW/XOR payload in one MIX buffer
 * (meta == pay). SHPX splits metadata in the MIX entry from compressed payload in
 * pool%04x.bin; the slice lives in the global SHPX pool buffer for the duration of the call.
 */
typedef struct {
	void const *meta;          /* Frame table + clip table (SHPX prefix in MIX, or whole blob) */
	ShpxPrefix const *pfx;     /* SHPX prefix view of meta; NULL when !is_shpx */
	void const *pay;           /* LCW/XOR payload: meta for monolithic, pool slice for SHPX */
	size_t meta_lim;           /* Byte limit for reads from meta (frame table, etc.) */
	size_t pay_lim;            /* Byte limit for reads from pay (pool slice or blob tail) */
	unsigned long table_base;  /* Frame table offset: sizeof(KeyFrameHeaderType) or SHPX ft_off */
	int is_shpx;               /* Non-zero when meta is an SHPX external-pool shape */
} KfBuildEnv;

static void KfBuildEnv_Done(KfBuildEnv *e)
{
	(void)e;
}

/* Frame-table u32: native BE read for SHPX, LE for legacy monolithic blobs. */
static unsigned long KfReadU32(const KfBuildEnv *e, const unsigned char *p)
{
	if (e->is_shpx) {
		uint32_t v;
		memcpy(&v, p, sizeof(v));
		return (unsigned long)v;
	}
	return ReadLE32(p);
}

/*
 * Fill KfBuildEnv for one Build_Frame invocation.
 * Returns 1 on success; 0 if SHPX pool slice load fails.
 * Non-SHPX shapes leave is_shpx clear and use dataptr for both meta and pay.
 */
static int KfBuildEnv_Init(void const *dataptr, size_t blob_size, KfBuildEnv *e)
{
	memset(e, 0, sizeof(*e));
	e->meta = dataptr;
	e->pay = dataptr;
	e->table_base = (unsigned long)sizeof(KeyFrameHeaderType);
	e->meta_lim = blob_size;
	e->pay_lim = blob_size;

	if (!SHPX_Is_Meta(dataptr))
		return 1;

	e->is_shpx = 1;
	e->pfx = SHPX_As_Prefix(dataptr);
	e->table_base = e->pfx->frame_table_offset;
	if (e->pfx->pool_data_size == 0u
	    || e->pfx->pool_data_size > SHPX_POOL_SLICE_MAX) {
		return 0;
	}
	e->pay = SHPX_Pool_Read_Slice(
	    e->pfx->pool_id,
	    e->pfx->pool_data_begin,
	    e->pfx->pool_data_size);
	if (!e->pay) {
		return 0;
	}
	e->pay_lim = (size_t)e->pfx->pool_data_size;
	if (blob_size > 0) {
		e->meta_lim = blob_size;
	} else {
		e->meta_lim = (size_t)e->pfx->clip_table_offset + (size_t)e->pfx->kf.frames * 8u;
	}
	return 1;
}

/*
 * Reload SUBFRAMEOFFS frame-table dwords for a chain step.
 * Monolithic blobs follow vanilla: read the full 28-byte window even past the
 * frame table into LCW payload. SHPX meta stops at clip_table_offset so clip
 * bytes are not mistaken for frame offsets (NUKE.SHP frame 8).
 */
static void KfReloadOffsetWindow(KfBuildEnv const *e, unsigned short currframe,
		unsigned long offset[SUBFRAMEOFFS])
{
	const unsigned char *row_bytes =
	    (const unsigned char *)Add_Long_To_Pointer(
	        e->meta, (((unsigned long)currframe << 3) + e->table_base));
	size_t row_off = (size_t)(((unsigned long)currframe << 3) + e->table_base);
	size_t table_end = 0;

	if (e->is_shpx && e->pfx)
		table_end = (size_t)e->pfx->clip_table_offset;

	for (int i = 0; i < SUBFRAMEOFFS; i++) {
		if (table_end != 0 && row_off + (size_t)(i + 1) * 4u > table_end)
			offset[i] = 0;
		else
			offset[i] = KfReadU32(e, row_bytes + i * 4);
	}
}
#endif

void *Get_Shape_Header_Data(void *ptr)
{
	if (UseBigShapeBuffer){

		ShapeHeaderType *header = (ShapeHeaderType*) ptr;
		return ((void*)  (header->shape_data + (long)(header->shape_buffer ? TheaterShapeBufferStart : BigShapeBufferStart) ) );

	}else{
		return (ptr);
	}
}

int Get_Last_Frame_Length(void)
{
	return(Length);
}



void Reset_Theater_Shapes (void)
{
	/*
	** Delete any previously allocated slots
	*/
	for (int i=THEATER_SLOT_START ; i<TheaterSlotsUsed ; i++){
		delete [] KeyFrameSlots [i];
	}

	TheaterShapeBufferPtr = TheaterShapeBufferStart;
	TotalTheaterShapes = 0;
	TheaterSlotsUsed = THEATER_SLOT_START;
}



void Reallocate_Big_Shape_Buffer(void)
{
	if (ReallocShapeBufferFlag){
		BigShapeBufferLength += 200 * 1024;							//Extra 2 Mb of uncompressed shape space
		BigShapeBufferPtr -= (unsigned)BigShapeBufferStart;
		Memory_Error = NULL;
		BigShapeBufferStart = (char*)Resize_Alloc(BigShapeBufferStart, BigShapeBufferLength);
		Memory_Error = &Memory_Error_Handler;
		/*
		** If we have run out of memory then disable the uncompressed shapes
		** It may still be possible to continue with compressed shapes
		*/
		if (!BigShapeBufferStart){
			UseBigShapeBuffer = false;
			return;
		}
		BigShapeBufferPtr += (unsigned)BigShapeBufferStart;
		ReallocShapeBufferFlag = FALSE;
	}
}




void Check_Use_Compressed_Shapes (void)
{
#ifdef WIN32
	MEMORYSTATUS	mem_info;

	mem_info.dwLength=sizeof(mem_info);
	GlobalMemoryStatus(&mem_info);

	UseBigShapeBuffer = (mem_info.dwTotalPhys > 16*1024*1024) ? TRUE : FALSE;
#else
	UseBigShapeBuffer = FALSE;
#endif
	OriginalUseBigShapeBuffer = UseBigShapeBuffer;

	// UseBigShapeBuffer = false;
}




/***********************************************************************************************
 * Disable_Uncompressed_Shapes -- Temporarily turns off shape decompression                    *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    11/19/96 2:37PM ST : Created                                                             *
 *=============================================================================================*/
void Disable_Uncompressed_Shapes (void)
{
	UseBigShapeBuffer = false;
}



/***********************************************************************************************
 * Enable_Uncompressed_Shapes -- Restores state of shape decompression before it was disabled  *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    11/19/96 2:37PM ST : Created                                                             *
 *=============================================================================================*/
void Enable_Uncompressed_Shapes (void)
{
	UseBigShapeBuffer = OriginalUseBigShapeBuffer;
}


#define FIXIT_SCORE_CRASH

/* XOR delta streams are variable-length; only require the start pointer to lie in-blob. */
static bool Build_Frame_DeltaPtrOk(void const *base, size_t blob, void const *src)
{
	if (blob == 0)
		return true;
	const unsigned char *b = (const unsigned char *)base;
	const unsigned char *s = (const unsigned char *)src;
	if (s < b)
		return false;
	return (size_t)(s - b) < blob;
}

/*
 * Counterpart to Get_Build_Frame_Field. The uncompressed-shape bookkeeping stores
 * a magic marker and a slot index back into the shape header, so the write must
 * use the same byte order the read expects — a native store would come back
 * byte-swapped on 68k and the marker would never match.
 */
static inline void Put_Build_Frame_Field(void *dataptr, size_t offset, unsigned short value)
{
	if (!dataptr) return;
#ifdef ATARI_ST
	if (SHPX_Is_Meta(dataptr)) {
		ShpxPrefix *pfx = (ShpxPrefix *)dataptr;
		uint16_t *field = (uint16_t *)((char *)&pfx->kf + offset);
		*field = value;
		return;
	}
#endif
	unsigned char *bytes = (unsigned char *)dataptr + offset;
	bytes[0] = (unsigned char)(value & 0xFFu);
	bytes[1] = (unsigned char)((value >> 8) & 0xFFu);
}

unsigned long Build_Frame(void const *dataptr, unsigned short framenumber, void *buffptr,
		size_t blob_size)
{
#ifdef FIXIT_SCORE_CRASH
	char * ptr;
	unsigned long offcurr, offdiff;
#else
	char * ptr, * lockptr;
	unsigned long offcurr, off16, offdiff;
#endif
	unsigned long offset[SUBFRAMEOFFS];
	unsigned long buffsize;
	unsigned short currframe = 0, subframe;
	unsigned long length = 0;
	char frameflags;
	unsigned long return_value;
	char *temp_shape_ptr;

	//
	// valid pointer??
	//
	Length = 0;
	if ( !dataptr || !buffptr ) {
		return(0);
	}

#ifdef ATARI_ST
	KfBuildEnv kf;
	if (!KfBuildEnv_Init(dataptr, blob_size, &kf)) {
		return (0);
	}
#define KF_META kf.meta
#define KF_PAY kf.pay
#define KF_META_LIM kf.meta_lim
#define KF_PAY_LIM kf.pay_lim
#define KF_TABLE kf.table_base
#define KF_U32(p) KfReadU32(&kf, (p))
#define KF_RETURN(v) \
	do { \
		unsigned long _kf_r = (unsigned long)(v); \
		KfBuildEnv_Done(&kf); \
		return _kf_r; \
	} while (0)
#else
#define KF_META dataptr
#define KF_PAY dataptr
#define KF_META_LIM blob_size
#define KF_PAY_LIM blob_size
#define KF_TABLE ((unsigned long)sizeof(KeyFrameHeaderType))
#define KF_U32(p) ReadLE32((p))
#define KF_RETURN(v) return (unsigned long)(v)
#endif

	//
	// look at header then check that frame to build is not greater
	// than total frames
	//
	unsigned short total_frames = Get_Build_Frame_Count(dataptr);
	
	if ( framenumber >= total_frames ) {
		KF_RETURN(0);
	}

	if (KF_META_LIM > 0) {
		size_t table_bytes = (size_t)KF_TABLE + ((size_t)total_frames << 3u);
		if (table_bytes > KF_META_LIM) {
			KF_RETURN(0);
		}
	}

	if (UseBigShapeBuffer
#ifdef ATARI_ST
	    && !kf.is_shpx
#endif
	){
		/*
		** If we havnt yet allocated memory for uncompressed shapes then do so now.
		**
		*/
		if (!BigShapeBufferStart){
			/*
			** Alloc calls Memory_Error on failure, which is fatal. Losing this
			** buffer only costs speed, so silence the handler and fall back to
			** compressed shapes instead of dying.
			*/
			void (*saved_memory_error)(void) = Memory_Error;
			Memory_Error = NULL;
			BigShapeBufferStart = (char*)Alloc(BigShapeBufferLength, MEM_NORMAL);
			if (BigShapeBufferStart){
				/*
				** Allocate memory for theater specific uncompressed shapes
				*/
				TheaterShapeBufferStart = (char*) Alloc (TheaterShapeBufferLength, MEM_NORMAL);
			}
			Memory_Error = saved_memory_error;

			if (!BigShapeBufferStart || !TheaterShapeBufferStart){
				if (TheaterShapeBufferStart){
					Free(TheaterShapeBufferStart);
					TheaterShapeBufferStart = NULL;
				}
				if (BigShapeBufferStart){
					Free(BigShapeBufferStart);
					BigShapeBufferStart = NULL;
				}
				UseBigShapeBuffer = false;
				OriginalUseBigShapeBuffer = false;
				DBG_WARN("Shapes: uncompressed buffer alloc failed, staying compressed");
			}else{
				BigShapeBufferPtr = BigShapeBufferStart;
				TheaterShapeBufferPtr = TheaterShapeBufferStart;
				DBG_INFO("Shapes: uncompressed frame cache at %p (%ld KiB) + theater %p (%ld KiB)",
				    (void*)BigShapeBufferStart, (long)BigShapeBufferLength / 1024L,
				    (void*)TheaterShapeBufferStart, (long)TheaterShapeBufferLength / 1024L);
			}
		}

		/* Allocation may have just failed; fall through to the compressed path. */
		if (BigShapeBufferStart){


		/*
		** Track memory usage in uncompressed shape buffers.
		*/
		static bool show_info = true;

		if ((Frame & 0xff) == 0){
			if (show_info){
				show_info = false;
			}
		} else {
			show_info = true;
		}


		/*
		** If we are running out of memory (<128k left) for uncompressed shapes
		** then allocate some more.
		*/
		if (( (unsigned)BigShapeBufferStart + BigShapeBufferLength) - (unsigned)BigShapeBufferPtr < 128*1024){
			ReallocShapeBufferFlag = TRUE;
		}

		/*
		** If this animation was not previously uncompressed then
		** allocate memory to keep the pointers to the uncompressed data
		** for these animation frames
		*/
		unsigned short keyfr_x = Get_Build_Frame_X(dataptr);
		if (keyfr_x != UNCOMPRESS_MAGIC_NUMBER){
			/*
			** Slots are split: normal shapes 0..THEATER_SLOT_START-1, theater
			** shapes above that. Running out is not fatal — drop back to
			** compressed shapes rather than writing past KeyFrameSlots.
			*/
			int const slot_limit = IsTheaterShape ? MAX_SLOTS : THEATER_SLOT_START;
			int const next_slot = IsTheaterShape ? TheaterSlotsUsed : TotalSlotsUsed;
			if (next_slot >= slot_limit){
				UseBigShapeBuffer = false;
				OriginalUseBigShapeBuffer = false;
				DBG_WARN("Shapes: keyframe slots exhausted (%d), back to compressed", next_slot);
			}else{
				unsigned short slot_index = (unsigned short)next_slot;
				if (IsTheaterShape){
					TheaterSlotsUsed++;
				}else{
					TotalSlotsUsed++;
				}
				Put_Build_Frame_Field(
				    (void*)dataptr, offsetof(KeyFrameHeaderType, x), UNCOMPRESS_MAGIC_NUMBER);
				Put_Build_Frame_Field(
				    (void*)dataptr, offsetof(KeyFrameHeaderType, y), slot_index);
				/*
				** Allocate and clear the memory for the shape info
				*/
				KeyFrameSlots[slot_index]= new char *[total_frames];
				memset (KeyFrameSlots[slot_index] , 0 , total_frames*sizeof(char*));
			}
		}

		/*
		** If this frame was previously uncompressed then just return
		** a pointer to the raw data
		*/
		unsigned short keyfr_y = Get_Build_Frame_Y(dataptr);
		if (UseBigShapeBuffer && keyfr_y < MAX_SLOTS && KeyFrameSlots[keyfr_y] != NULL
		    && *(KeyFrameSlots[keyfr_y]+framenumber)){
			if (IsTheaterShape){
				KF_RETURN((unsigned long)TheaterShapeBufferStart + (unsigned long)*(KeyFrameSlots[keyfr_y]+framenumber));
			}else{
				KF_RETURN((unsigned long)BigShapeBufferStart + (unsigned long)*(KeyFrameSlots[keyfr_y]+framenumber));
			}
		}
		}
	}

	// Linear frame bytes: width*height; TD SHP DeltaSize can be larger (decompress workspace).
	unsigned short width = Get_Build_Frame_Width(dataptr);
	unsigned short height = Get_Build_Frame_Height(dataptr);
	unsigned long wh = (unsigned long)width * (unsigned long)height;
	unsigned long lfs;
#ifdef ATARI_ST
	if (kf.is_shpx) {
		lfs = (unsigned long)kf.pfx->kf.largest_frame_size;
	} else
#endif
	{
		const unsigned char *hdrbytes = (const unsigned char *)KF_META;
		lfs = (unsigned long)ReadLE16(
		    hdrbytes + offsetof(KeyFrameHeaderType, largest_frame_size));
	}
	/* DeltaSize / largest_frame_size is max decompress buffer; XOR can index up to that. */
	buffsize = wh > lfs ? wh : lfs;
	if (buffsize > (unsigned long)(4 * 1024 * 1024)) {
		KF_RETURN(0);
	}

	// get offset into data
	unsigned long frame_offset = (((unsigned long)framenumber << 3) + KF_TABLE);

	if (KF_META_LIM > 0 && (size_t)frame_offset + 12u > KF_META_LIM) {
		KF_RETURN(0);
	}

	ptr = (char *)Add_Long_To_Pointer( KF_META, frame_offset );
	
	// Read 12 bytes (3 unsigned longs) from potentially unaligned ptr
	const unsigned char* offset_bytes = (const unsigned char*)ptr;
	offset[0] = KF_U32(offset_bytes);
	offset[1] = KF_U32(offset_bytes + 4);
	offset[2] = KF_U32(offset_bytes + 8);
	
	frameflags = (char)(offset[0] >> 24);

	short flags;
#ifdef ATARI_ST
	if (kf.is_shpx) {
		flags = kf.pfx->kf.flags;
	} else
#endif
	{
		const unsigned char *flags_bytes =
		    (const unsigned char *)KF_META + offsetof(KeyFrameHeaderType, flags);
		flags = (short)ReadLE16(flags_bytes);
	}

	if ( (frameflags & KF_KEYFRAME) ) {
		unsigned long data_offset = (offset[0] & 0x00FFFFFFL);

		if (KF_PAY_LIM > 0 && data_offset >= (unsigned long)KF_PAY_LIM) {
			KF_RETURN(0);
		}

		ptr = (char *)Add_Long_To_Pointer( KF_PAY, data_offset );

		if (flags & 1 ) {
			if (KF_PAY_LIM > 0 && data_offset + 768u > (unsigned long)KF_PAY_LIM) {
				KF_RETURN(0);
			}
			ptr = (char *)Add_Long_To_Pointer( ptr, 768L );
		}
		length = LCW_Uncompress( ptr, buffptr, buffsize );
	} else {	// key delta or delta

		if ( (frameflags & KF_DELTA) ) {
			/* Reference is frame index in low 24 bits (high byte is ReferenceFormat, not part of index). */
			unsigned long ref_frame = (unsigned long)(offset[1] & 0x00FFFFFFUL);
			if (ref_frame >= (unsigned long)total_frames) {
				KF_RETURN(0);
			}
			currframe = (unsigned short)ref_frame;

#ifdef ATARI_ST
			KfReloadOffsetWindow(&kf, currframe, offset);
#else
			ptr = (char *)Add_Long_To_Pointer( KF_META, (((unsigned long)currframe << 3) + KF_TABLE) );
			const unsigned char* offset_bytes = (const unsigned char*)ptr;
			for (int i = 0; i < SUBFRAMEOFFS; i++) {
				offset[i] = KF_U32(offset_bytes + i * 4);
			}
#endif
		}

		// key frame
		offcurr = offset[1] & 0x00FFFFFFL;

		// key delta
		offdiff = (offset[0] & 0x00FFFFFFL) - offcurr;

		if (KF_PAY_LIM > 0 && offcurr >= (unsigned long)KF_PAY_LIM) {
			KF_RETURN(0);
		}

		ptr = (char *)Add_Long_To_Pointer( KF_PAY, offcurr );

		if (flags & 1 ) {
			if (KF_PAY_LIM > 0 && offcurr + 768u > (unsigned long)KF_PAY_LIM) {
				KF_RETURN(0);
			}
			ptr = (char *)Add_Long_To_Pointer( ptr, 768L );
		}

#ifndef FIXIT_SCORE_CRASH
		off16 = (unsigned long)lockptr & 0x00003FFFL;
#endif
		length = LCW_Uncompress( ptr, buffptr, buffsize );

		if (length > buffsize) {
			KF_RETURN(0);
		}

#ifndef FIXIT_SCORE_CRASH
		if ( ((offset[2] & 0x00FFFFFFL) - offcurr) >= (0x00010000L - off16) ) {

			ptr = (char *)Add_Long_To_Pointer( ptr, offdiff );
			off16 = (unsigned long)ptr & 0x00003FFFL;

			offcurr += offdiff;
			offdiff = 0;
		}
#endif
		length = buffsize;
#if defined(DEBUG) && defined(BUILD_FRAME_XOR_TRACE)
		fprintf(stdout,
			"[Build_Frame] keydelta XOR: shape=%p fr=%u size wh=%lux%lu=%lu lfs=%lu "
			"buffsize=%lu LCW_out=%lu off0=%lX off1=%lX off2=%lX offcurr=%lX offdiff=%lX "
			"kfflags=0x%02X pal=%d buff=%p delta=%p frames=%u\n",
			dataptr, (unsigned)framenumber, (unsigned long)width, (unsigned long)height,
			(unsigned long)wh, (unsigned long)lfs, (unsigned long)buffsize,
			(unsigned long)length,
			(unsigned long)(offset[0] & 0x00FFFFFFUL),
			(unsigned long)(offset[1] & 0x00FFFFFFUL),
			(unsigned long)(offset[2] & 0x00FFFFFFUL),
			(unsigned long)offcurr, (unsigned long)offdiff,
			(unsigned)(unsigned char)frameflags, (int)flags, buffptr,
			Add_Long_To_Pointer(ptr, offdiff), (unsigned)total_frames);
		fflush(stdout);
#endif
		if (!Build_Frame_DeltaPtrOk(KF_PAY, KF_PAY_LIM,
					Add_Long_To_Pointer(ptr, offdiff))) {
			KF_RETURN(0);
		}
		Apply_Delta(buffptr, Add_Long_To_Pointer(ptr, offdiff), buffsize);

		if ( (frameflags & KF_DELTA) ) {
			// adjust to delta after the keydelta

			currframe++;
			subframe = 2;

			while (currframe <= framenumber) {
				offdiff = (offset[subframe] & 0x00FFFFFFL) - offcurr;

#ifndef FIXIT_SCORE_CRASH
				if ( ((offset[subframe+2] & 0x00FFFFFFL) - offcurr) >= (0x00010000L - off16) ) {

					ptr = (char *)Add_Long_To_Pointer( ptr, offdiff );
					off16 = (unsigned long)lockptr & 0x00003FFFL;

					offcurr += offdiff;
					offdiff = 0;
				}
#endif

				length = buffsize;
#if defined(DEBUG) && defined(BUILD_FRAME_XOR_TRACE)
				fprintf(stdout,
					"[Build_Frame] chain XOR: shape=%p fr=%u curr=%u sub=%u "
					"buffsize=%lu offcurr=%lX offdiff=%lX buff=%p delta=%p\n",
					dataptr, (unsigned)framenumber, (unsigned)currframe,
					(unsigned)subframe, (unsigned long)buffsize,
					(unsigned long)offcurr, (unsigned long)offdiff,
					buffptr,
					Add_Long_To_Pointer(ptr, offdiff));
				fflush(stdout);
#endif
				if (!Build_Frame_DeltaPtrOk(KF_PAY, KF_PAY_LIM,
							Add_Long_To_Pointer(ptr, offdiff))) {
					KF_RETURN(0);
				}
				Apply_Delta(buffptr, Add_Long_To_Pointer(ptr, offdiff),
					buffsize);

				currframe++;
				subframe += 2;

				if ( subframe >= (SUBFRAMEOFFS - 1) &&
					currframe <= framenumber ) {
#ifdef ATARI_ST
					KfReloadOffsetWindow(&kf, currframe, offset);
#else
					const unsigned char *row_bytes =
						(const unsigned char *)Add_Long_To_Pointer(
							KF_META,
							(((unsigned long)currframe << 3) + KF_TABLE));
					for (int i = 0; i < SUBFRAMEOFFS; i++) {
						offset[i] = KF_U32(row_bytes + i * 4);
					}
#endif
					subframe = 0;
				}
			}
		}
	}

	/*
	** Must mirror the guard on the slot-allocation block above: SHPX shapes are
	** excluded there and therefore never get a KeyFrameSlots entry, so storing
	** into one here would follow an uninitialised index.
	*/
	if (UseBigShapeBuffer
#ifdef ATARI_ST
	    && !kf.is_shpx
#endif
	){
		/*
		** Save the uncompressed shape data so we dont have to uncompress it
		** again next time its drawn.
		** We keep a space free before the raw shape data so we can add line
		** header info before the shape is drawn for the first time
		*/

		if (IsTheaterShape){
			/*
			** Shape is a theater specific shape
			*/
			return_value = (unsigned long) TheaterShapeBufferPtr;
			unsigned short height = Get_Build_Frame_Height(dataptr);
			temp_shape_ptr = TheaterShapeBufferPtr + height+sizeof(ShapeHeaderType);
			/*
			** align the actual shape data
			*/
			if (3 & (unsigned)temp_shape_ptr){
				temp_shape_ptr = (char *) ((unsigned)(temp_shape_ptr + 3) & 0xfffffffc);
			}

			memcpy (temp_shape_ptr , buffptr , length);
			((ShapeHeaderType *)TheaterShapeBufferPtr)->draw_flags = -1;						//Flag that headers need to be generated
			((ShapeHeaderType *)TheaterShapeBufferPtr)->shape_data = temp_shape_ptr - (unsigned)TheaterShapeBufferStart;		//pointer to old raw shape data
			((ShapeHeaderType *)TheaterShapeBufferPtr)->shape_buffer = 1;	//Theater buffer
			unsigned short y = Get_Build_Frame_Y(dataptr);
			if (y >= MAX_SLOTS || KeyFrameSlots[y] == NULL){
				/* No slot for this shape — return the decoded frame uncached. */
				Length = length;
				KF_RETURN(return_value);
			}
			*(KeyFrameSlots[y]+framenumber) = TheaterShapeBufferPtr - (unsigned)TheaterShapeBufferStart;
			TheaterShapeBufferPtr = (char*)(length + (unsigned)temp_shape_ptr);
			/*
			** Align the next shape
			*/
			if (3 & (unsigned)TheaterShapeBufferPtr){
				TheaterShapeBufferPtr = (char *)((unsigned)(TheaterShapeBufferPtr + 3) & 0xfffffffc);
			}
			Length = length;
			KF_RETURN(return_value);

		}else{


			return_value=(unsigned long)BigShapeBufferPtr;
			unsigned short height = Get_Build_Frame_Height(dataptr);
			temp_shape_ptr = BigShapeBufferPtr + height+sizeof(ShapeHeaderType);
			/*
			** align the actual shape data
			*/
			if (3 & (unsigned)temp_shape_ptr){
				temp_shape_ptr = (char *) ((unsigned)(temp_shape_ptr + 3) & 0xfffffffc);
			}
			memcpy (temp_shape_ptr , buffptr , length);
			((ShapeHeaderType *)BigShapeBufferPtr)->draw_flags = -1;						//Flag that headers need to be generated
			((ShapeHeaderType *)BigShapeBufferPtr)->shape_data = temp_shape_ptr - (unsigned)BigShapeBufferStart;		//pointer to old raw shape data
			((ShapeHeaderType *)BigShapeBufferPtr)->shape_buffer = 0;	//Normal Big Shape Buffer
			unsigned short y = Get_Build_Frame_Y(dataptr);
			if (y >= MAX_SLOTS || KeyFrameSlots[y] == NULL){
				/* No slot for this shape — return the decoded frame uncached. */
				Length = length;
				KF_RETURN(return_value);
			}
			*(KeyFrameSlots[y]+framenumber) = BigShapeBufferPtr - (unsigned)BigShapeBufferStart;
			BigShapeBufferPtr = (char*)(length + (unsigned)temp_shape_ptr);
			// Align the next shape
			if (3 & (unsigned)BigShapeBufferPtr){
				BigShapeBufferPtr = (char *)((unsigned)(BigShapeBufferPtr + 3) & 0xfffffffc);
			}
			Length = length;
			KF_RETURN(return_value);
		}

	}else{
		KF_RETURN((unsigned long)buffptr);
	}

#undef KF_META
#undef KF_PAY
#undef KF_META_LIM
#undef KF_PAY_LIM
#undef KF_TABLE
#undef KF_U32
}

unsigned long Build_Frame(void const *dataptr, unsigned short framenumber, void *buffptr)
{
	return Build_Frame(dataptr, framenumber, buffptr, (size_t)0);
}


static inline unsigned short Get_Build_Frame_Field(void const *dataptr, size_t offset)
{
	if (!dataptr) return 0;
	const unsigned char* bytes = (const unsigned char*)dataptr;
#ifdef ATARI_ST
	if (SHPX_Is_Meta(dataptr)) {
		ShpxPrefix const *pfx = SHPX_As_Prefix(dataptr);
		uint16_t const *field = (uint16_t const *)((char const *)&pfx->kf + offset);
		return *field;
	}
#endif
	return ReadLE16(bytes + offset);
}

/***********************************************************************************************
 * Get_Build_Frame_Count -- Fetches the number of frames in data block.                        *
 *                                                                                             *
 *    Use this routine to determine the number of shapes within the data block.                *
 *                                                                                             *
 * INPUT:   dataptr  -- Pointer to the keyframe shape data block.                              *
 *                                                                                             *
 * OUTPUT:  Returns with the number of shapes in the data block.                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   06/25/1995 JLB : Commented.                                                               *
 *=============================================================================================*/
unsigned short Get_Build_Frame_Count(void const *dataptr)
{
	return Get_Build_Frame_Field(dataptr, offsetof(KeyFrameHeaderType, frames));
}


unsigned short Get_Build_Frame_X(void const *dataptr)
{
	return Get_Build_Frame_Field(dataptr, offsetof(KeyFrameHeaderType, x));
}


unsigned short Get_Build_Frame_Y(void const *dataptr)
{
	return Get_Build_Frame_Field(dataptr, offsetof(KeyFrameHeaderType, y));
}


/***********************************************************************************************
 * Get_Build_Frame_Width -- Fetches the width of the shape image.                              *
 *                                                                                             *
 *    Use this routine to fetch the width of the shapes within the keyframe shape data block.  *
 *    All shapes within the block have the same width.                                         *
 *                                                                                             *
 * INPUT:   dataptr  -- Pointer to the keyframe shape data block.                              *
 *                                                                                             *
 * OUTPUT:  Returns with the width of the shapes in the block -- expressed in pixels.          *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   06/25/1995 JLB : Commented                                                                *
 *=============================================================================================*/
unsigned short Get_Build_Frame_Width(void const *dataptr)
{
	return Get_Build_Frame_Field(dataptr, offsetof(KeyFrameHeaderType, width));
}


/***********************************************************************************************
 * Get_Build_Frame_Height -- Fetches the height of the shape image.                            *
 *                                                                                             *
 *    Use this routine to fetch the height of the shapes within the keyframe shape data block. *
 *    All shapes within the block have the same height.                                        *
 *                                                                                             *
 * INPUT:   dataptr  -- Pointer to the keyframe shape data block.                              *
 *                                                                                             *
 * OUTPUT:  Returns with the height of the shapes in the block -- expressed in pixels.         *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   06/25/1995 JLB : Commented                                                                *
 *=============================================================================================*/
unsigned short Get_Build_Frame_Height(void const *dataptr)
{
	return Get_Build_Frame_Field(dataptr, offsetof(KeyFrameHeaderType, height));
}


/***********************************************************************************************
 * Get_Build_Frame_BufferBytes -- Bytes Build_Frame needs for buffptr.                         *
 *                                                                                             *
 *    Matches Build_Frame: max(width*height, largest_frame_size). Callers must pass a buffer   *
 *    at least this large or LCW / XOR delta steps corrupt memory.                             *
 *                                                                                             *
 * INPUT:   dataptr  -- Pointer to the keyframe shape data block.                              *
 *                                                                                             *
 * OUTPUT:  Byte count, or 0 if invalid / unreasonably large.                                  *
 *=============================================================================================*/
unsigned long Get_Build_Frame_BufferBytes(void const *dataptr)
{
	if (!dataptr)
		return 0;
	unsigned short width = Get_Build_Frame_Width(dataptr);
	unsigned short height = Get_Build_Frame_Height(dataptr);
	unsigned long wh = (unsigned long)width * (unsigned long)height;
	unsigned long lfs;
#ifdef ATARI_ST
	if (SHPX_Is_Meta(dataptr)) {
		lfs = (unsigned long)SHPX_As_Prefix(dataptr)->kf.largest_frame_size;
	} else
#endif
	{
		const unsigned char *hdrbytes = (const unsigned char *)dataptr;
		lfs = (unsigned long)ReadLE16(
		    hdrbytes + offsetof(KeyFrameHeaderType, largest_frame_size));
	}
	unsigned long buffsize = wh > lfs ? wh : lfs;
	if (buffsize > (unsigned long)(4 * 1024 * 1024))
		return 0;
	return buffsize;
}


bool Get_Build_Frame_Palette(void const * dataptr, void * palette)
{
	if (!dataptr) return(false);
#ifdef ATARI_ST
	if (SHPX_Is_Meta(dataptr))
		return false;
#endif
	
	// Read flags as little-endian
	const unsigned char* flags_bytes = (const unsigned char*)dataptr + offsetof(KeyFrameHeaderType, flags);
	short flags = (short)ReadLE16(flags_bytes);
	if (!(flags & 1)) return(false);
	
	unsigned short frames = Get_Build_Frame_Count(dataptr);
	char const * ptr = (char const *)Add_Long_To_Pointer( dataptr,
						( (( (long)sizeof(unsigned long) << 1) * frames ) +
						16 + sizeof(KeyFrameHeaderType) ) );

	memcpy(palette, ptr, 768L);
	return(true);
}