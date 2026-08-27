/*
 * digi_ring.h - Soft PCM ring with pluggable consumer_pos (STE DMA vs Timer A ISR).
 */
#ifndef DIGI_RING_H
#define DIGI_RING_H

#ifdef ATARI_ST

#ifdef __cplusplus
extern "C" {
#endif

enum { DIGI_RING_BYTES = 1024 };

typedef struct DigiRing DigiRing;

typedef struct DigiRingOps {
	unsigned (*consumer_pos)(DigiRing* r);
	void (*arm)(DigiRing* r);
	void (*stop)(DigiRing* r);
} DigiRingOps;

struct DigiRing {
	unsigned char* base;
	unsigned size;
	unsigned write_pos;
	unsigned queued;
	unsigned last_consumer;
	int armed;
	DigiRingOps const* ops;
	void* hw_ctx;
};

void digi_ring_init(DigiRing* r, unsigned char* base, unsigned size, DigiRingOps const* ops);
void digi_ring_reset(DigiRing* r);
void digi_ring_sync(DigiRing* r);
unsigned digi_ring_free_bytes(DigiRing* r);
/* Write up to nbytes of device-native bytes; returns bytes written (may be partial). */
unsigned digi_ring_write_available(DigiRing* r, unsigned char const* src, unsigned nbytes);
void digi_ring_silence(DigiRing* r, unsigned char fill);

#ifdef __cplusplus
}
#endif

#endif /* ATARI_ST */

#endif /* DIGI_RING_H */
