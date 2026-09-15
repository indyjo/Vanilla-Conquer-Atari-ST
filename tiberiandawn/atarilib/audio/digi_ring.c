/*
 * digi_ring.c - Fill from write_pos toward the hardware play cursor.
 * Free = (play - write - 1) mod size; one slot stays empty.
 */
#ifdef ATARI_ST

#include "audio/digi_ring.h"

#include <string.h>

void digi_ring_init(DigiRing* r, unsigned char* base, unsigned size, DigiRingOps const* ops)
{
	if (!r) {
		return;
	}
	r->base = base;
	r->size = size ? size : (unsigned)DIGI_RING_BYTES;
	r->ops = ops;
	r->hw_ctx = 0;
	digi_ring_reset(r);
}

void digi_ring_reset(DigiRing* r)
{
	if (!r) {
		return;
	}
	r->write_pos = 0;
	r->last_play = 0;
	r->armed = 0;
}

void digi_ring_silence(DigiRing* r, unsigned char fill)
{
	if (!r || !r->base) {
		return;
	}
	memset(r->base, (int)fill, r->size);
}

static unsigned digi_ring_play(DigiRing* r)
{
	unsigned play;

	if (!r->ops || !r->ops->consumer_pos) {
		return r->last_play;
	}
	play = r->ops->consumer_pos(r);
	if (play >= r->size) {
		return r->last_play;
	}
	r->last_play = play;
	return play;
}

unsigned digi_ring_free_bytes(DigiRing* r)
{
	unsigned play;

	if (!r || !r->base || r->size < 2u) {
		return 0;
	}
	if (!r->armed) {
		return r->size - 1u;
	}
	play = digi_ring_play(r);
	return (play + r->size - r->write_pos - 1u) % r->size;
}

static void digi_ring_write_bytes(DigiRing* r, unsigned char const* src, unsigned nbytes)
{
	unsigned off = r->write_pos;

	while (nbytes) {
		unsigned to_end = r->size - off;
		unsigned batch = nbytes < to_end ? nbytes : to_end;
		memcpy(r->base + off, src, batch);
		src += batch;
		nbytes -= batch;
		off += batch;
		if (off >= r->size) {
			off = 0;
		}
	}
	r->write_pos = off;
}

unsigned digi_ring_write_available(DigiRing* r, unsigned char const* src, unsigned nbytes)
{
	unsigned freeb;
	unsigned n;

	if (!r || !r->base || !src || nbytes == 0) {
		return 0;
	}

	freeb = digi_ring_free_bytes(r);
	n = nbytes < freeb ? nbytes : freeb;
	if (n == 0) {
		return 0;
	}
	digi_ring_write_bytes(r, src, n);
	if (!r->armed) {
		r->last_play = 0;
		if (r->ops && r->ops->arm) {
			r->ops->arm(r);
		}
		r->armed = 1;
	}
	return n;
}

#endif /* ATARI_ST */
