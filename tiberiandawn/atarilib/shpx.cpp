/*
 * shpx.cpp — SHPX pool I/O and metadata helpers (Atari ST).
 */

#include "shpx.h"

#include "function.h"

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

void *SHPX_Pool_Read_Slice(uint16_t pool_id, uint32_t begin, uint32_t size)
{
	char name[16];
	CCFileClass file;

	if (size == 0 || pool_id == 0 || size > SHPX_POOL_SLICE_MAX)
		return NULL;

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
