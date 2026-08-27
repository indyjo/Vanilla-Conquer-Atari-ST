/*
 * digi_ring.c - Soft ring free-space accounting via DigiRingOps::consumer_pos.
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
	r->queued = 0;
	r->last_consumer = 0;
	r->armed = 0;
}

void digi_ring_silence(DigiRing* r, unsigned char fill)
{
	if (!r || !r->base) {
		return;
	}
	memset(r->base, (int)fill, r->size);
}

void digi_ring_sync(DigiRing* r)
{
	unsigned consumer;
	unsigned played;

	if (!r || !r->base || !r->ops || !r->ops->consumer_pos || !r->armed) {
		return;
	}
	consumer = r->ops->consumer_pos(r);
	if (consumer >= r->size) {
		return;
	}
	played = (consumer + r->size - r->last_consumer) % r->size;
	r->last_consumer = consumer;
	if (played >= r->queued) {
		r->queued = 0;
		r->write_pos = consumer;
	} else {
		r->queued -= played;
	}
}

unsigned digi_ring_free_bytes(DigiRing* r)
{
	if (!r || !r->base) {
		return 0;
	}
	if (!r->armed) {
		return r->size > 0 ? r->size - 1u : 0;
	}
	digi_ring_sync(r);
	if (r->queued >= r->size - 1u) {
		return 0;
	}
	return (r->size - 1u) - r->queued;
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

static unsigned digi_ring_cold_arm(DigiRing* r, unsigned n)
{
	r->write_pos = 0;
	r->queued = 0;
	r->last_consumer = 0;
	r->queued = n;
	if (r->ops && r->ops->arm) {
		r->ops->arm(r);
	}
	r->armed = 1;
	if (r->ops && r->ops->consumer_pos) {
		r->last_consumer = r->ops->consumer_pos(r);
	}
	return n;
}

unsigned digi_ring_write_available(DigiRing* r, unsigned char const* src, unsigned nbytes)
{
	unsigned freeb;
	unsigned n;

	if (!r || !r->base || !src || nbytes == 0) {
		return 0;
	}

	if (!r->armed) {
		n = nbytes < r->size - 1u ? nbytes : r->size - 1u;
		digi_ring_write_bytes(r, src, n);
		return digi_ring_cold_arm(r, n);
	}

	freeb = digi_ring_free_bytes(r);
	n = nbytes < freeb ? nbytes : freeb;
	if (n == 0) {
		return 0;
	}
	digi_ring_write_bytes(r, src, n);
	r->queued += n;
	return n;
}

#endif /* ATARI_ST */
