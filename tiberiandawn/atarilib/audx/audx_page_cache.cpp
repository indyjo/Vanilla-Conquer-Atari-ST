/*
 * audx_page_cache.cpp — RankCache page slabs + reserved stream slabs + pins.
 *
 * Cache: 32 shards × 6 × 1024 B = 192 KiB (directory only; pointers into slab).
 * Stream: 16 × 1024 B reserved for > AUDX_PAGE_CACHE_MAX fills (not RankCache).
 * Pin counts are main-thread only; eviction skips pinned cache pages.
 */

#include "audx_page_cache.h"

#include "audx_pool_file.h"
#include "rankcache.h"

#include <new>
#include <string.h>

struct AudxPageKey {
	uint16_t pool_id;
	uint32_t page_index;

	bool operator==(AudxPageKey const &o) const
	{
		return pool_id == o.pool_id && page_index == o.page_index;
	}
};

using AudxPageRankCache = RankCache<AudxPageKey, uint8_t *>;

enum {
	AUDX_CACHE_PAGES = (int)AUDX_PAGE_CACHE_SHARDS * (int)AUDX_PAGE_CACHE_SHARD_SIZE,
	AUDX_SLAB_PAGES = AUDX_CACHE_PAGES + (int)AUDX_PAGE_STREAM_COUNT
};

static int g_audx_page_inited;
static uint8_t *g_audx_page_slab;
static uint8_t g_audx_page_pin[AUDX_SLAB_PAGES];
static AudxPageRankCache *g_audx_page_shards[AUDX_PAGE_CACHE_SHARDS];

static unsigned audx_page_shard_index(AudxPageKey const &key)
{
	uint32_t bits = ((uint32_t)key.pool_id << 16) ^ key.page_index;
	return (unsigned)(bits & (AUDX_PAGE_CACHE_SHARDS - 1u));
}

static int audx_slab_index(uint8_t const *page)
{
	size_t off;

	if (!g_audx_page_inited || !g_audx_page_slab || !page)
		return -1;
	if (page < g_audx_page_slab)
		return -1;
	off = (size_t)(page - g_audx_page_slab);
	if ((off % (size_t)AUDX_PAGE_SIZE) != 0)
		return -1;
	off /= (size_t)AUDX_PAGE_SIZE;
	if (off >= (size_t)AUDX_SLAB_PAGES)
		return -1;
	return (int)off;
}

static int audx_page_unpinned(uint8_t *page)
{
	int const i = audx_slab_index(page);
	return i >= 0 && g_audx_page_pin[i] == 0;
}

int AUDX_Page_Cache_Is_Inited(void)
{
	return g_audx_page_inited;
}

void AUDX_Page_Cache_Shutdown(void)
{
	for (unsigned sh = 0; sh < AUDX_PAGE_CACHE_SHARDS; ++sh) {
		delete g_audx_page_shards[sh];
		g_audx_page_shards[sh] = 0;
	}
	delete[] g_audx_page_slab;
	g_audx_page_slab = 0;
	memset(g_audx_page_pin, 0, sizeof(g_audx_page_pin));
	g_audx_page_inited = 0;
	AUDX_Pool_Close_All();
}

int AUDX_Page_Cache_Init(void)
{
	if (g_audx_page_inited)
		return 0;

	g_audx_page_slab = new (std::nothrow) uint8_t[(size_t)AUDX_SLAB_PAGES * (size_t)AUDX_PAGE_SIZE];
	if (!g_audx_page_slab)
		return -1;
	memset(g_audx_page_slab, 0, (size_t)AUDX_SLAB_PAGES * (size_t)AUDX_PAGE_SIZE);
	memset(g_audx_page_pin, 0, sizeof(g_audx_page_pin));

	for (unsigned sh = 0; sh < AUDX_PAGE_CACHE_SHARDS; ++sh) {
		g_audx_page_shards[sh] = new (std::nothrow) AudxPageRankCache((uint16_t)AUDX_PAGE_CACHE_SHARD_SIZE);
		if (!g_audx_page_shards[sh] || !g_audx_page_shards[sh]->valid()) {
			AUDX_Page_Cache_Shutdown();
			return -1;
		}
		for (unsigned slot = 0; slot < AUDX_PAGE_CACHE_SHARD_SIZE; ++slot) {
			unsigned const global = sh * AUDX_PAGE_CACHE_SHARD_SIZE + slot;
			AudxPageKey seed;
			seed.pool_id = 0;
			seed.page_index = (uint32_t)global;
			g_audx_page_shards[sh]->set((uint16_t)slot, seed, g_audx_page_slab + (size_t)global * AUDX_PAGE_SIZE);
		}
		g_audx_page_shards[sh]->reset_head();
	}

	g_audx_page_inited = 1;
	return 0;
}

void AUDX_Page_Pin(uint8_t const *page)
{
	int const i = audx_slab_index(page);
	if (i < 0)
		return;
	if (g_audx_page_pin[i] < 255u)
		g_audx_page_pin[i]++;
}

void AUDX_Page_Unpin(uint8_t const *page)
{
	int const i = audx_slab_index(page);
	if (i < 0)
		return;
	if (g_audx_page_pin[i] > 0)
		g_audx_page_pin[i]--;
}

uint8_t *AUDX_Stream_Page_Acquire(void)
{
	int i;

	if (!g_audx_page_inited || !g_audx_page_slab)
		return 0;
	for (i = AUDX_CACHE_PAGES; i < AUDX_SLAB_PAGES; ++i) {
		if (g_audx_page_pin[i] == 0)
			return g_audx_page_slab + (size_t)i * AUDX_PAGE_SIZE;
	}
	return 0;
}

uint8_t const *AUDX_Page_Get(uint16_t pool_id, uint32_t file_offset)
{
	AudxPageKey key;
	uint8_t *page = 0;
	unsigned sh;
	AudxPageRankCache *rank;
	uint32_t page_begin;

	if (!g_audx_page_inited || pool_id == 0)
		return 0;

	key.pool_id = pool_id;
	key.page_index = file_offset / AUDX_PAGE_SIZE;
	sh = audx_page_shard_index(key);
	rank = g_audx_page_shards[sh];
	if (!rank)
		return 0;

	if (rank->get(key, page) && page)
		return page;

	if (!rank->retarget_oldest_evictable(key, page, audx_page_unpinned) || !page)
		return 0;

	page_begin = key.page_index * AUDX_PAGE_SIZE;
	if (!AUDX_Pool_Read(pool_id, page_begin, AUDX_PAGE_SIZE, page)) {
		/* Leave slot keyed but zeroed so a retry can refill. */
		memset(page, 0, AUDX_PAGE_SIZE);
		return 0;
	}
	return page;
}
