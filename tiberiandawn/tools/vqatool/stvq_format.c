/*
 * stvq_format.c - STVQ header helpers.
 */
#include "stvq_format.h"

#include <string.h>

void stvq_write_be16(unsigned char *p, uint16_t v)
{
	p[0] = (unsigned char)((v >> 8) & 0xffu);
	p[1] = (unsigned char)(v & 0xffu);
}

void stvq_write_be32(unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char)((v >> 24) & 0xffu);
	p[1] = (unsigned char)((v >> 16) & 0xffu);
	p[2] = (unsigned char)((v >> 8) & 0xffu);
	p[3] = (unsigned char)(v & 0xffu);
}

uint16_t stvq_read_be16(const unsigned char *p)
{
	return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

uint32_t stvq_read_be32(const unsigned char *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

void stvq_header_pack(unsigned char out[STVQ_STHD_SIZE], const StvqHeader *h)
{
	memset(out, 0, STVQ_STHD_SIZE);
	stvq_write_be16(out + 0, h->version);
	stvq_write_be16(out + 2, h->flags);
	stvq_write_be16(out + 4, h->frames);
	stvq_write_be16(out + 6, h->width);
	stvq_write_be16(out + 8, h->height);
	out[10] = h->block_w;
	out[11] = h->block_h;
	out[12] = h->fps;
	out[13] = h->reserved0;
	stvq_write_be16(out + 14, h->cb_entries);
	stvq_write_be16(out + 16, h->sample_rate);
	out[18] = h->channels;
	out[19] = h->bits_per_sample;
	stvq_write_be16(out + 20, h->max_frame_bytes);
	stvq_write_be16(out + 22, h->reserved1);
	stvq_write_be32(out + 24, h->reserved2);
}

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

/* Pack one 0..63 VGA channel to STE nibble (LSB in bit 3). Matches
 * St_Pack_ST_HW_From_Rgb6_Channel in atarilib/st_temperat_palette.h. */
static uint16_t ste_pack_channel(uint8_t c6)
{
	uint16_t c4 = (uint16_t)((((unsigned)c6 & 63u) * 15u + 31u) / 63u);
	return (uint16_t)(((c4 >> 1) & 7u) | ((c4 & 1u) << 3));
}

static uint8_t ste_unpack_channel(uint16_t nib)
{
	uint8_t c4 = (uint8_t)(((nib & 7u) << 1) | ((nib >> 3) & 1u));
	return (uint8_t)((c4 << 4) | c4); /* 4-bit → 8-bit */
}

uint16_t stvq_vga6_to_ste(uint8_t r6, uint8_t g6, uint8_t b6)
{
	return (uint16_t)((ste_pack_channel(r6) << 8) | (ste_pack_channel(g6) << 4) |
			  ste_pack_channel(b6));
}

void stvq_ste_to_rgb24(uint16_t ste, uint8_t *r, uint8_t *g, uint8_t *b)
{
	*r = ste_unpack_channel((uint16_t)((ste >> 8) & 0xfu));
	*g = ste_unpack_channel((uint16_t)((ste >> 4) & 0xfu));
	*b = ste_unpack_channel((uint16_t)(ste & 0xfu));
}
