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
	r->last_hz200 = 0;
	r->have_hz200 = 0;
	r->armed = 0;
}

void digi_ring_silence(DigiRing* r, unsigned char fill)
{
	if (!r || !r->base) {
		return;
	}
	memset(r->base, (int)fill, r->size);
}

static unsigned digi_ring_ahead(DigiRing const* r, unsigned consumer)
{
	return (r->write_pos + r->size - consumer) % r->size;
}

void digi_ring_sync(DigiRing* r)
{
	unsigned consumer;
	unsigned last;
	unsigned played;
	unsigned to_wr;
	unsigned max_played;
	unsigned long now;
	unsigned long dt;

	if (!r || !r->base || !r->ops || !r->ops->consumer_pos || !r->armed) {
		return;
	}
	consumer = r->ops->consumer_pos(r);
	if (consumer >= r->size) {
		return;
	}
	last = r->last_consumer;
	played = (consumer + r->size - last) % r->size;

	now = *(volatile unsigned long*)0x4BAUL;
	if (!r->have_hz200) {
		r->last_hz200 = now;
		r->have_hz200 = 1;
		max_played = r->size / 2u;
	} else {
		dt = now - r->last_hz200;
		r->last_hz200 = now;
		/* Ceiling for 25033 Hz (125 samples per 200 Hz tick) plus slack. */
		max_played = (unsigned)(dt * 128ul + 64ul);
		if (max_played > r->size - 1u) {
			max_played = r->size - 1u;
		}
	}

	/*
	 * Full ring: write_pos is one byte behind the play head. A torn or
	 * 1-byte-backward DMA pointer then yields played == size-1 >= queued
	 * and used to snap write_pos, after which Capacity looked empty and
	 * the next submit lapped live samples.
	 */
	if (played > max_played && played > (r->size / 4u)) {
		consumer = last;
		played = 0;
	}

	to_wr = digi_ring_ahead(r, last);
	if ((r->queued == 0 && to_wr == 0) || (played > 0 && to_wr > 0 && to_wr <= played)) {
		r->write_pos = consumer;
		r->last_consumer = consumer;
		r->queued = 0;
		return;
	}

	r->last_consumer = consumer;
	r->queued = digi_ring_ahead(r, consumer);
	if (r->queued > r->size - 1u) {
		r->queued = r->size - 1u;
	}
}

unsigned digi_ring_free_bytes(DigiRing* r)
{
	unsigned gap;
	unsigned geo_free;
	unsigned acct_free;

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
	acct_free = (r->size - 1u) - r->queued;
	gap = digi_ring_ahead(r, r->last_consumer);
	if (gap >= r->size - 1u) {
		geo_free = 0;
	} else {
		geo_free = (r->size - 1u) - gap;
	}
	return geo_free < acct_free ? geo_free : acct_free;
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
	/* write_pos already advanced by digi_ring_write_bytes; do not reset it. */
	r->queued = n;
	r->last_consumer = 0;
	r->have_hz200 = 0;
	if (r->ops && r->ops->arm) {
		r->ops->arm(r);
	}
	r->armed = 1;
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
	r->queued = digi_ring_ahead(r, r->last_consumer);
	if (r->queued > r->size - 1u) {
		r->queued = r->size - 1u;
	}
	return n;
}

#endif /* ATARI_ST */
