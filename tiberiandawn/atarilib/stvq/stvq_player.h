/*
 * stvq_player.h - Stream STVQ frames into the back screen buffer.
 */
#ifndef STVQ_PLAYER_H
#define STVQ_PLAYER_H

#include "stvq_format.h"
#include "stvq_hw.h"
#include "stvq_io.h"
#include "stvq_prof.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct StvqFrame {
	int have_stpl;
	uint16_t stpl[16];
	/* Points into player frame_buf SND0 body; valid until next next_frame. */
	const unsigned char *pcm;
	size_t pcm_len;
} StvqFrame;

typedef struct StvqPlayer {
	const StvqIo *io;
	StvqHeader hdr;
	uint16_t tiles_x;
	uint16_t tiles_y;
	uint8_t *codebook; /* cb_entries * 32 */
	int frame_index;
	StvqHw *hw;
	uint16_t initial_pal[16];
	/*
	 * Prefetch invariant: IO sits after the next top-level chunk header;
	 * next_size is that chunk's size. next_size==0 means finished (STEN).
	 */
	uint32_t next_size;
	/* Scratch: STFR payload + following 8-byte header (single read). */
	unsigned char *frame_buf;
	size_t frame_cap;
	/* After read_frame: payload ready to decode (size = STFR body, got = bytes read). */
	int load_ready;
	uint32_t load_size;
	size_t load_got;
	StvqProf *prof; /* optional; filled each next_frame */
} StvqPlayer;

/* Open via caller-owned IO (already positioned at start). Does not close IO. */
int stvq_player_open(StvqPlayer *p, StvqHw *hw, const StvqIo *io);
void stvq_player_close(StvqPlayer *p);

/* Set when stvq_player_open returns -1 (static string). */
extern const char *stvq_player_open_error;

/*
 * Read next STFR payload (+ following header) into frame_buf. Does not decode.
 * Returns 1 ok, 0 at end (next_size==0), -1 error.
 * No-op (returns 1) if load_ready already set (e.g. prefetched).
 * Safe to call after pcm for the previous frame has been copied out of frame_buf.
 */
int stvq_player_read_frame(StvqPlayer *p);

/*
 * Decode load_ready payload into hw back buffer. Fills out (palette + pcm in frame_buf).
 * Returns 1 ok, -1 error. Clears load_ready.
 */
int stvq_player_decode_frame(StvqPlayer *p, StvqFrame *out);

/*
 * read_frame + decode_frame (read is a no-op when already prefetched).
 * Returns 1 ok, 0 at end, -1 error.
 * Submit out->pcm via stvq_hw_pcm_start before the next read/prefetch.
 */
int stvq_player_next_frame(StvqPlayer *p, StvqFrame *out);

#ifdef __cplusplus
}
#endif

#endif /* STVQ_PLAYER_H */
