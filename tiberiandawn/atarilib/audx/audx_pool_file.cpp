/*
 * audx_pool_file.cpp — Read spans from pool%04x.bin sidecars.
 *
 * Main thread only (page-ring service / Play_Sample). Never call from VBL.
 * Keep a few pool files open across page-cache misses. CCFileClass objects are
 * embedded and reused (Close + Open) — no heap new/delete on the play path.
 */

#include "audx_pool_file.h"

#include "audx.h"
#include "function.h"

#include <string.h>

enum { AUDX_POOL_OPEN_SLOTS = 4 };

struct AudxPoolSlot {
	uint16_t pool_id;
	uint32_t next_off; /* absolute file offset after last successful Read (0 = unknown) */
	int in_use;
	CCFileClass file;
};

static AudxPoolSlot g_audx_pools[AUDX_POOL_OPEN_SLOTS];

int AUDX_Format_Pool_Name(uint16_t pool_id, char *out, size_t out_cap)
{
	if (!out || out_cap < 13u || pool_id == 0)
		return 0;
	if (snprintf(out, out_cap, "pool%04x.bin", (unsigned)pool_id) >= (int)out_cap)
		return 0;
	return 1;
}

static void audx_pool_slot_close(AudxPoolSlot *slot)
{
	if (!slot || !slot->in_use)
		return;
	if (slot->file.Is_Open())
		slot->file.Close();
	slot->in_use = 0;
	slot->pool_id = 0;
	slot->next_off = 0;
}

void AUDX_Pool_Close_All(void)
{
	for (int i = 0; i < AUDX_POOL_OPEN_SLOTS; ++i)
		audx_pool_slot_close(&g_audx_pools[i]);
}

static AudxPoolSlot *audx_pool_find(uint16_t pool_id)
{
	for (int i = 0; i < AUDX_POOL_OPEN_SLOTS; ++i) {
		if (g_audx_pools[i].in_use && g_audx_pools[i].pool_id == pool_id)
			return &g_audx_pools[i];
	}
	return 0;
}

static AudxPoolSlot *audx_pool_open(uint16_t pool_id)
{
	char name[16];
	AudxPoolSlot *slot;
	int i;

	slot = audx_pool_find(pool_id);
	if (slot)
		return slot;

	if (!AUDX_Format_Pool_Name(pool_id, name, sizeof(name)))
		return 0;

	/* Prefer an empty slot; otherwise recycle slot 0 (simple, rare with 3 pools). */
	slot = 0;
	for (i = 0; i < AUDX_POOL_OPEN_SLOTS; ++i) {
		if (!g_audx_pools[i].in_use) {
			slot = &g_audx_pools[i];
			break;
		}
	}
	if (!slot) {
		slot = &g_audx_pools[0];
		audx_pool_slot_close(slot);
	}

	if (slot->file.Is_Open())
		slot->file.Close();
	if (!slot->file.Open(name, READ))
		return 0;
	slot->pool_id = pool_id;
	slot->in_use = 1;
	slot->next_off = 0;
	return slot;
}

int AUDX_Pool_Read(uint16_t pool_id, uint32_t begin, uint32_t size, void *dst)
{
	AudxPoolSlot *slot;
	CCFileClass *f;
	long got;

	if (!dst || size == 0 || pool_id == 0)
		return 0;

	slot = audx_pool_open(pool_id);
	if (!slot || !slot->in_use)
		return 0;
	f = &slot->file;

	/* Sequential page fills can skip Seek when the file cursor is already at begin. */
	if (slot->next_off != begin) {
		if (f->Seek((long)begin, SEEK_SET) != (long)begin) {
			slot->next_off = 0;
			return 0;
		}
	}
	got = f->Read(dst, (long)size);
	if (got < 0) {
		slot->next_off = 0;
		return 0;
	}
	if ((uint32_t)got < size) {
		/* EOF short read: pad so callers always get a full page buffer. */
		memset((unsigned char *)dst + got, 0, (size_t)(size - (uint32_t)got));
	}
	if (got == 0 && size != 0) {
		/* Past EOF — nothing to deliver. */
		slot->next_off = 0;
		return 0;
	}
	slot->next_off = begin + (uint32_t)got;
	return 1;
}
