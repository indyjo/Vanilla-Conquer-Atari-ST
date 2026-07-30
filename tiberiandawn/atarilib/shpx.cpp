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
enum { SHPX_POOL_MAX_ID = 8 };
enum { SHPX_POOL_STATE_UNTRIED = 0, SHPX_POOL_STATE_RESIDENT = 1, SHPX_POOL_STATE_STREAMED = 2 };
/* Keep this much free after loading, so the shape and sprite caches still fit. */
#define SHPX_POOL_RAM_HEADROOM (16L * 1024L * 1024L)

static struct {
	int state;
	uint8_t *base;
	uint32_t size;
} g_shpx_pool[SHPX_POOL_MAX_ID + 1];

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

	long const free_bytes = Total_Ram_Free(MEM_NORMAL);
	if (free_bytes < (long)file_size + SHPX_POOL_RAM_HEADROOM) {
		DBG_INFO("SHPX: %s stays streamed (%ld KiB free, need %ld KiB + headroom)",
		    name, free_bytes / 1024L, (long)file_size / 1024L);
		return;
	}

	/*
	 * Alloc calls Memory_Error on failure, which is fatal. Residency is only a
	 * speed-up, so silence it and fall back to streaming.
	 */
	void (*saved_memory_error)(void) = Memory_Error;
	Memory_Error = NULL;
	uint8_t *const base = (uint8_t *)Alloc((unsigned long)file_size, MEM_NORMAL);
	Memory_Error = saved_memory_error;
	if (!base) {
		DBG_WARN("SHPX: %s alloc failed (%ld KiB), staying streamed", name, (long)file_size / 1024L);
		return;
	}

	if (!file.Open(READ)) {
		Free(base);
		return;
	}
	int const got = file.Read(base, file_size);
	file.Close();

	if (got != file_size) {
		DBG_WARN("SHPX: %s short read (%d of %d), staying streamed", name, got, file_size);
		Free(base);
		return;
	}

	g_shpx_pool[pool_id].base = base;
	g_shpx_pool[pool_id].size = (uint32_t)file_size;
	g_shpx_pool[pool_id].state = SHPX_POOL_STATE_RESIDENT;
	DBG_INFO("SHPX: %s resident at %p (%ld KiB)", name, (void *)base, (long)file_size / 1024L);
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
