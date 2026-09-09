/*
 * shpx.cpp — SHPX pool I/O and metadata helpers (Atari ST).
 */

#include "shpx.h"

#include "function.h"
#include "debugstring.h"
#include "memflag.h"

#include <string.h>

typedef char ShpxLayout_Check[(sizeof(ShpxPrefix) == SHPX_PREFIX_SIZE) ? 1 : -1];
typedef char ShpxClip_Check[(sizeof(ShpxClipEntry) == 8u) ? 1 : -1];

static int shpx_format_pool_name(uint16_t pool_id, char *out, size_t out_cap)
{
	if (!out || out_cap < 13u)
		return 0;
	if (snprintf(out, out_cap, "pool%04x.bin", (unsigned)pool_id) >= (int)out_cap)
		return 0;
	return 1;
}

static struct {
	int loaded;
	uint16_t pool_id;
	uint32_t begin;
	uint32_t size;
	uint8_t buf[SHPX_POOL_SLICE_MAX];
} g_shpx_pool_slice;

/*
 * Whole-pool residency.
 *
 * SHPX streams shape payloads from poolnnnn.bin through the single slice buffer
 * above, which is what lets the game run on a 4 MB STE. The cache holds exactly
 * one (pool_id, begin, size) triple, so alternating sprites cost a full
 * open/seek/read/close per draw. On a machine with memory to spare, read each
 * pool once and hand out pointers into it instead; the caller only needs `size`
 * bytes readable at the returned address, so it cannot tell the difference.
 *
 * Machines without the headroom keep the streaming path unchanged.
 */
/*
 * SHPX sidecars are 1 CONQUER, 2 TEMPERAT, 3 DESERT, 4 WINTER (atari.md,
 * "pool_id assignment"). Ids 5-7 are AUDX audio and go through
 * audx_pool_file.cpp instead. Resident worst case is CONQUER (~2.0 MB) plus one
 * theatre (<=0.15 MB); the theatres are mutually exclusive in a session.
 */
enum { SHPX_POOL_MAX_ID = 4 };
enum { SHPX_POOL_STATE_UNTRIED = 0, SHPX_POOL_STATE_RESIDENT = 1, SHPX_POOL_STATE_STREAMED = 2 };
/*
 * Where the pool goes, and when it is worth it.
 *
 * TT-RAM is what every other cache on this port competes for: Alloc() is
 * malloc(), the PRG flags allow alternate RAM, so the shape cache, the MIX
 * caches and the preshift table all land there and only spill into ST-RAM once
 * TT-RAM runs dry. Taking 2 MB off the top for a pool would push them out, so
 * TT-RAM is used only when there is still SHPX_POOL_TT_HEADROOM left for them
 * afterwards -- matched to SHP_GROW_HEADROOM in st16_preshift.cpp, where the
 * preshift cache stops growing.
 *
 * ST-RAM is the opposite case: after startup nothing on this port allocates
 * from it (screen pages, STVQ ping-pong and the audio DMA ring are all taken
 * before the first shape is drawn), so on a 4 MB ST + 4 MB TT machine roughly
 * 3.9 MB sits idle there while TT-RAM is full. Putting the pool in ST-RAM
 * displaces nothing; it is slower to read than TT-RAM, but far cheaper than
 * the open/seek/read/close it replaces. The smaller headroom leaves room for a
 * cutscene's buffers.
 *
 * Neither fits -- a 4 MB machine -- means the streaming path stays.
 */
#ifndef SHPX_POOL_TT_HEADROOM
#define SHPX_POOL_TT_HEADROOM (4L * 1024L * 1024L)
#endif
#ifndef SHPX_POOL_ST_HEADROOM
#define SHPX_POOL_ST_HEADROOM (1L * 1024L * 1024L)
#endif

static struct {
	int state;
	uint8_t *base;
	uint32_t size;
} g_shpx_pool[SHPX_POOL_MAX_ID + 1];

enum { SHPX_POOL_MEM_NONE = 0, SHPX_POOL_MEM_ST = 1, SHPX_POOL_MEM_TT = 2 };

/*
 * Pick the pool that can hold the file without crowding its usual tenants.
 * Returns the block and sets *mem to where it came from, or NULL to stay
 * streamed.
 */
static uint8_t *shpx_alloc_pool_block(long file_size, int *mem)
{
	long st_largest = 0L;
	long tt_largest = 0L;
	uint8_t *base;

	*mem = SHPX_POOL_MEM_NONE;
	ST_Largest_Free_Blocks(&st_largest, &tt_largest);

	if (tt_largest >= file_size + SHPX_POOL_TT_HEADROOM) {
		base = (uint8_t *)Ttram_Alloc((unsigned long)file_size);
		if (base != NULL) {
			*mem = SHPX_POOL_MEM_TT;
			return base;
		}
	}

	if (st_largest >= file_size + SHPX_POOL_ST_HEADROOM) {
		base = (uint8_t *)Stram_Alloc((unsigned long)file_size);
		if (base != NULL) {
			*mem = SHPX_POOL_MEM_ST;
			return base;
		}
	}

	DBG_INFO("SHPX: no room for %ld KiB (largest free: ST %ld KiB, TT %ld KiB)",
	    file_size / 1024L, st_largest / 1024L, tt_largest / 1024L);
	return NULL;
}

static void shpx_free_pool_block(uint8_t *base, int mem)
{
	if (mem == SHPX_POOL_MEM_TT)
		Ttram_Free(base);
	else
		Stram_Free(base);
}

static void shpx_try_load_whole_pool(uint16_t pool_id)
{
	char name[16];
	CCFileClass file;

	g_shpx_pool[pool_id].state = SHPX_POOL_STATE_STREAMED;

	if (!shpx_format_pool_name(pool_id, name, sizeof(name)))
		return;

	file.Set_Name(name);
	if (!file.Is_Available())
		return;

	int const file_size = file.Size();
	if (file_size <= 0)
		return;

	/*
	 * GEMDOS block rather than Alloc(): the choice of pool has to be explicit,
	 * a 2 MB block with program lifetime has no business fragmenting the C
	 * heap, and Mxalloc failing is just a NULL -- no Memory_Error to silence.
	 */
	int mem = SHPX_POOL_MEM_NONE;
	uint8_t *const base = shpx_alloc_pool_block((long)file_size, &mem);
	if (!base) {
		DBG_INFO("SHPX: %s stays streamed (%ld KiB)", name, (long)file_size / 1024L);
		return;
	}

	if (!file.Open(READ)) {
		shpx_free_pool_block(base, mem);
		return;
	}
	int const got = file.Read(base, file_size);
	file.Close();

	if (got != file_size) {
		DBG_WARN("SHPX: %s short read (%d of %d), staying streamed", name, got, file_size);
		shpx_free_pool_block(base, mem);
		return;
	}

	g_shpx_pool[pool_id].base = base;
	g_shpx_pool[pool_id].size = (uint32_t)file_size;
	g_shpx_pool[pool_id].state = SHPX_POOL_STATE_RESIDENT;
	DBG_INFO("SHPX: %s resident in %s at %p (%ld KiB)",
	    name, (mem == SHPX_POOL_MEM_TT) ? "TT-RAM" : "ST-RAM",
	    (void *)base, (long)file_size / 1024L);
}

void *SHPX_Pool_Read_Slice(uint16_t pool_id, uint32_t begin, uint32_t size)
{
	char name[16];
	CCFileClass file;

	if (size == 0 || pool_id == 0 || size > SHPX_POOL_SLICE_MAX)
		return NULL;

	if (pool_id <= SHPX_POOL_MAX_ID) {
		if (g_shpx_pool[pool_id].state == SHPX_POOL_STATE_UNTRIED)
			shpx_try_load_whole_pool(pool_id);
		if (g_shpx_pool[pool_id].state == SHPX_POOL_STATE_RESIDENT) {
			/* begin + size cannot wrap: both are bounded by the checks above. */
			if ((unsigned long)begin + (unsigned long)size
			    > (unsigned long)g_shpx_pool[pool_id].size) {
				return NULL;
			}
			return g_shpx_pool[pool_id].base + begin;
		}
	}

	if (g_shpx_pool_slice.loaded
	    && g_shpx_pool_slice.pool_id == pool_id
	    && g_shpx_pool_slice.begin == begin
	    && g_shpx_pool_slice.size == size) {
		return g_shpx_pool_slice.buf;
	}

	if (!shpx_format_pool_name(pool_id, name, sizeof(name)))
		return NULL;

	file.Set_Name(name);
	if (!file.Is_Available())
		return NULL;
	if (!file.Open(READ))
		return NULL;

	if (file.Seek((long)begin, SEEK_SET) != (long)begin) {
		file.Close();
		return NULL;
	}

	if (file.Read(g_shpx_pool_slice.buf, (long)size) != (long)size) {
		file.Close();
		return NULL;
	}
	file.Close();

	g_shpx_pool_slice.loaded = 1;
	g_shpx_pool_slice.pool_id = pool_id;
	g_shpx_pool_slice.begin = begin;
	g_shpx_pool_slice.size = size;
	return g_shpx_pool_slice.buf;
}

int SHPX_Get_Frame_Clip(void const *meta, unsigned frame, uint16_t *cx, uint16_t *cy, uint16_t *cw,
    uint16_t *ch)
{
	ShpxPrefix const *pfx;
	ShpxClipEntry const *clips;

	if (!meta || !cx || !cy || !cw || !ch || !SHPX_Is_Meta(meta))
		return 0;
	pfx = SHPX_As_Prefix(meta);
	if (frame >= (unsigned)pfx->kf.frames)
		return 0;
	clips = (ShpxClipEntry const *)((char const *)meta + (size_t)pfx->clip_table_offset);
	*cx = clips[frame].x;
	*cy = clips[frame].y;
	*cw = clips[frame].w;
	*ch = clips[frame].h;
	return 1;
}
