/*
 * remix_vqa_omit.c — MIX entries that must not survive a repack.
 *
 * TRANSIT.MIX is registered before MOVIES.MIX. The same filename CRC is found
 * in the first mix, so a leftover Westwood VQA there hides the STVQ copy.
 */
#include "remix_vqa.h"

#include <strings.h>

static const uint32_t k_transit_omit_vqa_crcs[] = {
	0x21DD332Fu, /* NOD1PRE.VQA — briefing after choosing Nod */
};

int remix_vqa_should_omit_entry(const char *mix_basename, uint32_t crc)
{
	size_t i;

	if (!mix_basename || strcasecmp(mix_basename, "TRANSIT.MIX") != 0)
		return 0;
	for (i = 0; i < sizeof(k_transit_omit_vqa_crcs) / sizeof(k_transit_omit_vqa_crcs[0]); ++i) {
		if (k_transit_omit_vqa_crcs[i] == crc)
			return 1;
	}
	return 0;
}
