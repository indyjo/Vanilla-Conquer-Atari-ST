/*
 * stvq_player.h - Stream STVQ frames into the back screen buffer.
 */
#ifndef STVQVIEW_PLAYER_H
#define STVQVIEW_PLAYER_H

#include "stvq_format.h"
#include "stvq_hw.h"
#include "stvq_prof.h"

#include <stdio.h>

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
	FILE *fp;
	StvqHeader hdr;
	unsigned tiles_x;
	unsigned tiles_y;
	uint8_t *codebook; /* cb_entries * 32 */
	int frame_index;
	int eof;
	StvqHw *hw;
	uint16_t initial_pal[16];
	/* Scratch for one STFR payload (single fread). */
	unsigned char *frame_buf;
	size_t frame_cap;
	StvqProf *prof; /* optional; filled each next_frame */
} StvqPlayer;

int stvq_player_open(StvqPlayer *p, StvqHw *hw, const char *path);
void stvq_player_close(StvqPlayer *p);

/* Set when stvq_player_open returns -1 (static string). */
extern const char *stvq_player_open_error;

/*
 * Decode next STFR into hw back buffer. Fills out (palette + pcm pointer into frame_buf).
 * Returns 1 ok, 0 at STEN/EOF, -1 error.
 * Reads the whole STFR payload with one fread, then parses in memory.
 * Submit out->pcm via stvq_hw_pcm_start before calling next_frame again.
 */
int stvq_player_next_frame(StvqPlayer *p, StvqFrame *out);

#ifdef __cplusplus
}
#endif

#endif /* STVQVIEW_PLAYER_H */
