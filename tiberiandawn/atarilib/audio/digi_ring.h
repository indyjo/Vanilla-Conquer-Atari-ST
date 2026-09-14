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
 * Software producer cursor over a looping hardware ring. One byte is never
 * queued (free = size-1-queued) so a full buffer is distinct from empty.
 * digi_ring_sync() reads ops->consumer_pos. queued is the forward distance
 * from the consumer to write_pos (never size, so 0 is empty). A true
 * underrun (DMA walked onto write_pos) parks write_pos on the consumer.
 * A 1-byte backward glitch at the full watermark (write_pos == consumer-1)
 * used to look like played==size-1 and wipe the queue; that is ignored.
 */
struct DigiRing {
	unsigned char* base;     /* device-native samples; DMA must be ST-RAM */
	unsigned size;           /* power of two, typically DIGI_RING_BYTES */
	unsigned write_pos;      /* next producer index in [0, size) */
	unsigned queued;         /* unplayed bytes; 0 = empty, max size-1 */
	unsigned last_consumer;  /* consumer offset at last sync (armed only) */
	unsigned long last_hz200; /* _hz_200 at last sync; bounds played */
	int have_hz200;
	int armed;               /* 1 after first write (ops->arm has run) */
	DigiRingOps const* ops;
	void* hw_ctx;            /* unused; reserved for a backend cookie */
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
