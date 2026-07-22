/*
 * stvq_format.c - STVQ header helpers (viewer).
 */
#include "stvq_format.h"

#include <string.h>

void stvq_header_unpack(const unsigned char in[STVQ_STHD_SIZE], StvqHeader *h)
{
	memset(h, 0, sizeof(*h));
	h->version = stvq_read_be16(in + 0);
	h->flags = stvq_read_be16(in + 2);
	h->frames = stvq_read_be16(in + 4);
	h->width = stvq_read_be16(in + 6);
	h->height = stvq_read_be16(in + 8);
	h->block_w = in[10];
	h->block_h = in[11];
	h->fps = in[12];
	h->reserved0 = in[13];
	h->cb_entries = stvq_read_be16(in + 14);
	h->sample_rate = stvq_read_be16(in + 16);
	h->channels = in[18];
	h->bits_per_sample = in[19];
	h->max_frame_bytes = stvq_read_be16(in + 20);
	h->reserved1 = stvq_read_be16(in + 22);
	h->reserved2 = stvq_read_be32(in + 24);
}

unsigned stvq_tiles_x(unsigned width)
{
	return (width + 7u) / 8u;
}

unsigned stvq_tiles_y(unsigned height)
{
	return (height + 7u) / 8u;
}

unsigned stvq_iff_padded(uint32_t size)
{
	return (unsigned)((size + 1u) & ~1u);
}
