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
	/* Byte offset of the hardware read cursor in [0, size). DMA: frame counter;
	 * Timer A: USP play pointer. */
	unsigned (*consumer_pos)(DigiRing* r);
	/* Start looping playback of [base, base+size). */
	void (*arm)(DigiRing* r);
	void (*stop)(DigiRing* r);
} DigiRingOps;

/*
 * write_pos stays ahead of the play cursor. Free space is
 * (play - write - 1) mod size, so one slot is never filled (empty ≠ full).
 */
struct DigiRing {
	unsigned char* base;     /* device-native samples; DMA must be ST-RAM */
	unsigned size;           /* power of two, typically DIGI_RING_BYTES */
	unsigned write_pos;      /* next producer index in [0, size) */
	unsigned last_play;      /* last in-range consumer (DMA read fail) */
	int armed;               /* 1 after first write (ops->arm has run) */
	DigiRingOps const* ops;
	void* hw_ctx;            /* unused; reserved for a backend cookie */
};

void digi_ring_init(DigiRing* r, unsigned char* base, unsigned size, DigiRingOps const* ops);
void digi_ring_reset(DigiRing* r);
unsigned digi_ring_free_bytes(DigiRing* r);
/* Write up to nbytes of device-native bytes; returns bytes written (may be partial). */
unsigned digi_ring_write_available(DigiRing* r, unsigned char const* src, unsigned nbytes);
void digi_ring_silence(DigiRing* r, unsigned char fill);

#ifdef __cplusplus
}
#endif

#endif /* ATARI_ST */

#endif /* DIGI_RING_H */
