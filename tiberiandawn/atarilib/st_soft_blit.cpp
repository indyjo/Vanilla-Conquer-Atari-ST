/*
 * ST software blitter: interprets ST_Blitter register state (BLIT_iT semantics).
 */

#include "st_blit.h"

static ST_Soft_Backend g_soft_backend;

ST_Soft_Backend::ST_Soft_Backend() : ST_Blit_Backend(state_)
{
}

ST_Soft_Backend &ST_Blit_Soft_Backend()
{
	return g_soft_backend;
}

static uint16_t ST_Soft_Apply_Op(uint8_t op, uint16_t d, uint16_t s, uint16_t mask)
{
	const uint16_t dm = (uint16_t)(d & mask);
	const uint16_t sm = (uint16_t)(s & mask);
	const uint16_t keep = (uint16_t)(d & ~mask);

	switch (op) {
	case 1:
		return (uint16_t)((dm & sm) | keep);
	case 3:
		return (uint16_t)(sm | keep);
	case 7:
		return (uint16_t)((dm | sm) | keep);
	default:
		return d;
	}
}

static uint16_t ST_Soft_Endmask(
	uint16_t x_count,
	uint16_t endmask1,
	uint16_t endmask2,
	uint16_t endmask3,
	uint16_t wi)
{
	// Hatari: first word or single-word line -> endmask1; last word -> endmask3; else endmask2.
	// reverse_x is handled in prepare by swapping endmask1/endmask3 in the register image.
	if (x_count == 1 || wi == 0) {
		return endmask1;
	}
	if (wi + 1 == x_count) {
		return endmask3;
	}
	return endmask2;
}

void ST_Soft_Backend::Await()
{
}

void ST_Soft_Backend::Execute(bool hog, uint16_t lines, void *src_addr, void *dst_addr)
{
	(void)hog;
	volatile ST_Blitter &r = Regs();
	r.src_addr = src_addr;
	r.dst_addr = dst_addr;
	r.y_count = lines;

	const int16_t src_x_inc = r.src_x_inc;
	const int16_t dst_x_inc = r.dst_x_inc;
	const int16_t src_y_inc = r.src_y_inc;
	const int16_t dst_y_inc = r.dst_y_inc;
	const uint16_t x_count = r.x_count;
	const uint16_t y_count = lines;
	const uint8_t op = r.op;
	const uint16_t endmask1 = r.endmask1;
	const uint16_t endmask2 = r.endmask2;
	const uint16_t endmask3 = r.endmask3;
	const unsigned shift = (unsigned)(r.skew & 15u);
	const bool fxsr = (r.skew & 0x80u) != 0;
	const bool nfsr = (r.skew & 0x40u) != 0;
	const bool reverse_x = src_x_inc < 0;

	uint8_t *s = (uint8_t *)src_addr;
	uint8_t *d = (uint8_t *)dst_addr;
	union {
		uint32_t u32;
		struct {
			uint16_t left;
			uint16_t right;
		} u16;
	} hold;
	
	for (uint16_t line = 0; line < y_count; ++line) {

		// Force additional source read to prime the hold register
		if (fxsr && x_count > 0) {
			if (reverse_x) {
				hold.u16.left = *(uint16_t *)s;
			} else {
				hold.u16.right = *(uint16_t *)s;
			}
			s += src_x_inc;
		}

		for (uint16_t wi = 0; wi < x_count; ++wi) {
			const bool last_word = (wi + 1) == x_count;
			if (!reverse_x) {
				hold.u32 = (hold.u32 << 16) | (hold.u32 >> 16);
			}
			if (!nfsr || !last_word) {
				hold.u16.right = *(uint16_t *)s;
				s += src_x_inc;
			}
			if (reverse_x) {
				hold.u32 = (hold.u32 << 16) | (hold.u32 >> 16);
			}

			const uint16_t sval = (uint16_t)(hold.u32 >> shift);
			const uint16_t mask = ST_Soft_Endmask(x_count, endmask1, endmask2, endmask3, wi);
			const uint16_t dval = *(uint16_t *)d;

			*(uint16_t *)d = ST_Soft_Apply_Op(op, dval, sval, mask);
			d += dst_x_inc;
		}

		s += src_y_inc - src_x_inc;
		d += dst_y_inc - dst_x_inc;
	}
}
