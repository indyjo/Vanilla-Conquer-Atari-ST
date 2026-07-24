/*
 * stvq_play.h - Host STVQ decode (RGB24 + PCM for preview).
 */
#ifndef STVQ_PLAY_H
#define STVQ_PLAY_H

#include "stvq_format.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct StvqPlayer {
	FILE *fp;
	StvqHeader hdr;
	unsigned tiles_x;
	unsigned tiles_y;
	unsigned tiles_n;
	uint16_t palette[16]; /* host-endian STE words */
	uint8_t *codebook;    /* cb_entries * 32 */
	uint8_t *tiles[2];    /* [0]=N-1, [1]=N-2 after first frames */
	uint8_t *work_tiles;  /* decode target */
	int frame_index;
	uint8_t *rgb;         /* width*height*3 */
	uint8_t *pcm;         /* last frame audio (owned, realloc'd) */
	size_t pcm_cap;
	size_t pcm_len;
	uint16_t *stcr_idx; /* codebook indices replaced this frame (STCR) */
	unsigned stcr_n;
	unsigned stcr_cap;
	int eof;
} StvqPlayer;

int stvq_player_open(StvqPlayer *p, const char *path);
void stvq_player_close(StvqPlayer *p);

/* Decode next STFR into p->rgb and p->pcm. Returns 1 ok, 0 at STEN/EOF, -1 error. */
int stvq_player_next_frame(StvqPlayer *p);

#ifdef __cplusplus
}
#endif

#endif /* STVQ_PLAY_H */
