/*
 * page_region_cache.cpp — 168 KiB linear SHPX region cache with whole-tier steal.
 */

#include "page_region_cache.h"

#include "memflag.h"
#include "rankcache.h"

#include <new>
#include <string.h>

enum {
	PRC_T48 = 0,
	PRC_T24 = 1,
	PRC_T8 = 2,
	PRC_TIER_COUNT = 3,
	PRC_MAX_SLOTS = 3
};

enum {
	PRC_BUDGET_48 = 48 * 1024,
	PRC_BUDGET_24 = 24 * 1024,
	PRC_BUDGET_8 = 8 * 1024
};

struct PrcHeader {
	uint16_t pool_id;
	uint32_t begin;
	uint32_t size;
	uint8_t *payload;
};

using PrcRankCache = RankCache<uint32_t, PrcHeader *>;

struct PrcKeyPred {
	uint16_t pool_id;
	uint32_t begin;
	uint32_t size;

	bool operator()(PrcHeader *h) const
	{
		return h && h->pool_id == pool_id && h->begin == begin && h->size == size;
	}
};

static const uint32_t g_prc_budget[PRC_TIER_COUNT] = {
	PRC_BUDGET_48, PRC_BUDGET_24, PRC_BUDGET_8
};
static const uint16_t g_prc_nslots[PRC_TIER_COUNT] = { 2, 2, 3 };
typedef char PrcSlabFit[(2 * 48 + 2 * 24 + 8 * 3) * 1024 == (int)PAGE_REGION_CACHE_BYTES ? 1 : -1];

static int g_prc_inited;
static uint8_t *g_prc_slab;
static PrcHeader g_prc_hdr[PRC_TIER_COUNT][PRC_MAX_SLOTS];
static PrcRankCache *g_prc_rank[PRC_TIER_COUNT];
/* -1 none; 0 T48 both slots are one steal window; 1.. T24/T8 also stolen. */
static int g_prc_stolen_until = -1;

static uint32_t prc_hash(uint16_t pool_id, uint32_t begin, uint32_t size)
{
	uint32_t h = begin;
	h ^= (uint32_t)pool_id * 0x9e3779b9u;
	h ^= size * 0x85ebca6bu;
	h ^= h >> 16;
	if (h == 0)
		h = 1;
	return h;
}

static int prc_tier_for_size(uint32_t size)
{
	if (size <= PRC_BUDGET_8)
		return PRC_T8;
	if (size <= PRC_BUDGET_24)
		return PRC_T24;
	return PRC_T48;
}

static int prc_steal_until_for_size(uint32_t size)
{
	uint32_t span;
	int t;

	if (size <= (uint32_t)PRC_BUDGET_48)
		return -1;

	span = (uint32_t)g_prc_nslots[PRC_T48] * g_prc_budget[PRC_T48];
	if (size <= span)
		return PRC_T48;

	for (t = 1; t < PRC_TIER_COUNT; ++t) {
		span += (uint32_t)g_prc_nslots[t] * g_prc_budget[t];
		if (size <= span)
			return t;
	}
	return PRC_TIER_COUNT - 1;
}

static int prc_tier_stolen(int t)
{
	return t >= 1 && g_prc_stolen_until >= 1 && t <= g_prc_stolen_until;
}

static void prc_reseed_tier(int t)
{
	PrcRankCache *rank;
	uint16_t n;
	uint16_t i;

	rank = g_prc_rank[t];
	if (!rank || !rank->valid())
		return;
	n = g_prc_nslots[t];
	for (i = 0; i < n; ++i) {
		g_prc_hdr[t][i].pool_id = 0;
		g_prc_hdr[t][i].begin = 0;
		g_prc_hdr[t][i].size = 0;
		rank->set(i, 0, &g_prc_hdr[t][i]);
	}
}

static void prc_set_steal_until(int until)
{
	int t;
	int first_new;
	int const old = g_prc_stolen_until;

	if (until < 0) {
		if (old >= 0)
			prc_reseed_tier(PRC_T48);
		if (old >= 1) {
			for (t = 1; t <= old; ++t)
				prc_reseed_tier(t);
		}
		g_prc_stolen_until = -1;
		return;
	}

	if (old < 0)
		prc_reseed_tier(PRC_T48);

	if (old > until && until >= 0) {
		for (t = until + 1; t <= old; ++t) {
			if (t >= 1)
				prc_reseed_tier(t);
		}
	}

	first_new = (old >= 1) ? old + 1 : 1;
	for (t = first_new; t <= until; ++t)
		prc_reseed_tier(t);

	g_prc_stolen_until = until;
}

static void prc_bind_steal_slot0(uint32_t hash, PrcHeader **hdr_out)
{
	PrcRankCache *rank = g_prc_rank[PRC_T48];
	uint16_t i;

	*hdr_out = &g_prc_hdr[PRC_T48][0];
	if (!rank)
		return;
	rank->set(0, hash, &g_prc_hdr[PRC_T48][0]);
	for (i = 1; i < g_prc_nslots[PRC_T48]; ++i)
		rank->set(i, 0, &g_prc_hdr[PRC_T48][i]);
}

static void prc_restore_from_stolen(void)
{
	int i;

	for (i = 0; i < PRC_TIER_COUNT; ++i)
		prc_reseed_tier(i);
	g_prc_stolen_until = -1;
}

void Page_Region_Cache_Init(void)
{
	int t;
	uint16_t i;
	uint8_t *p;

	if (g_prc_inited)
		return;

	if (!g_prc_slab) {
		g_prc_slab = (uint8_t *)Alloc((unsigned long)PAGE_REGION_CACHE_BYTES, MEM_NORMAL);
		if (!g_prc_slab)
			return;
	}

	p = g_prc_slab;
	for (t = 0; t < PRC_TIER_COUNT; ++t) {
		if (!g_prc_rank[t]) {
			g_prc_rank[t] = new (std::nothrow) PrcRankCache(g_prc_nslots[t]);
			if (!g_prc_rank[t] || !g_prc_rank[t]->valid())
				return;
		}
		for (i = 0; i < g_prc_nslots[t]; ++i) {
			g_prc_hdr[t][i].pool_id = 0;
			g_prc_hdr[t][i].begin = 0;
			g_prc_hdr[t][i].size = 0;
			g_prc_hdr[t][i].payload = p;
			g_prc_rank[t]->set(i, 0, &g_prc_hdr[t][i]);
			p += g_prc_budget[t];
		}
	}

	g_prc_stolen_until = -1;
	g_prc_inited = 1;
}

void *Page_Region_Cache_Get(uint16_t pool_id, uint32_t begin, uint32_t size,
    Page_Region_Cache_Fill fill, void *ctx)
{
	int tier;
	int steal_until;
	uint32_t hash;
	PrcHeader *hdr = 0;
	PrcKeyPred pred;
	PrcRankCache *rank;

	if (!g_prc_inited)
		Page_Region_Cache_Init();
	if (!g_prc_inited || !fill || pool_id == 0 || size == 0 || size > PAGE_REGION_CACHE_BYTES)
		return 0;

	steal_until = prc_steal_until_for_size(size);
	tier = (steal_until >= 0) ? PRC_T48 : prc_tier_for_size(size);

	if (steal_until < 0) {
		if (prc_tier_stolen(tier))
			prc_restore_from_stolen();
		else if (tier == PRC_T48 && g_prc_stolen_until >= 0)
			prc_set_steal_until(-1);
	}

	rank = g_prc_rank[tier];
	if (!rank || !rank->valid())
		return 0;

	hash = prc_hash(pool_id, begin, size);
	pred.pool_id = pool_id;
	pred.begin = begin;
	pred.size = size;

	if (rank->get(hash, hdr, pred) && hdr && hdr->payload)
		return hdr->payload;

	if (steal_until >= 0) {
		prc_set_steal_until(steal_until);
		prc_bind_steal_slot0(hash, &hdr);
		if (!hdr || !hdr->payload)
			return 0;
	} else if (!rank->retarget_oldest(hash, hdr, pred) || !hdr || !hdr->payload) {
		return 0;
	}

	if (fill(ctx, pool_id, begin, size, hdr->payload) != 0) {
		hdr->pool_id = 0;
		hdr->begin = 0;
		hdr->size = 0;
		return 0;
	}

	hdr->pool_id = pool_id;
	hdr->begin = begin;
	hdr->size = size;
	return hdr->payload;
}
