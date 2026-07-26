/*
 * stvqview.c - Standalone FORM 'STVQ' player for Atari ST/STE.
 *
 * Usage: stvqview.ttp file.stv
 * Keys: ESC quit, Space pause/resume.
 * Writes STVQPROF.TXT with 200Hz timing on exit.
 *
 * Thin CLI over atarilib/stvq (FILE* StvqIo adapter; private screens).
 */
#include "stvq_format.h"
#include "stvq_hw.h"
#include "stvq_io.h"
#include "stvq_player.h"
#include "stvq_prof.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static int file_read(void *user, void *buf, size_t n)
{
	FILE *fp = (FILE *)user;
	unsigned char *p = (unsigned char *)buf;
	size_t got = 0;
	while (got < n) {
		size_t r = fread(p + got, 1, n - got, fp);
		if (r == 0)
			return -1;
		got += r;
	}
	return 0;
}

static int file_seek(void *user, long off, int whence)
{
	FILE *fp = (FILE *)user;
	return fseek(fp, off, whence) == 0 ? 0 : -1;
}

static void usage(void)
{
	printf("usage: stvqview.ttp <file.stv>\n");
	printf("  ESC quit, Space pause/resume\n");
}

static void dump_prof(const StvqProf *prof)
{
	FILE *fp;

	stvq_prof_print(prof, stdout);
	fflush(stdout);

	fp = fopen("STVQPROF.TXT", "w");
	if (fp) {
		stvq_prof_print(prof, fp);
		fclose(fp);
		printf("(also wrote STVQPROF.TXT)\n");
	}
}

static int play(const char *path)
{
	FILE *fp;
	StvqIo io;
	StvqHw hw;
	StvqPlayer player;
	StvqFrame frame;
	StvqProf prof;
	int paused = 0;
	int first = 1;
	int have_frame = 0;
	int use_audio;
	unsigned vbl_accum = 0;
	unsigned vbls_per_frame;
	int rc = 1;
	unsigned long t_frame0 = 0;
	unsigned long t_prev_present = 0;

	memset(&io, 0, sizeof(io));
	memset(&hw, 0, sizeof(hw));
	memset(&player, 0, sizeof(player));
	memset(&frame, 0, sizeof(frame));
	stvq_prof_reset(&prof);

	fp = fopen(path, "rb");
	if (!fp) {
		printf("error: cannot open %s (%s)\n", path, strerror(errno));
		return 1;
	}

	io.user = fp;
	io.read = file_read;
	io.seek = file_seek;

	if (stvq_player_open(&player, &hw, &io) != 0) {
		printf("error: cannot open %s as STVQ (%s)\n",
		    path,
		    stvq_player_open_error ? stvq_player_open_error : "unknown");
		fclose(fp);
		return 1;
	}

	player.prof = &prof;
	{
		unsigned fps = player.hdr.fps ? player.hdr.fps : 15u;
		prof.budget_ticks = (STVQ_HZ200_PER_SEC + fps / 2u) / fps;
		if (!prof.budget_ticks)
			prof.budget_ticks = 1;
	}

	printf("STVQ %u x %u %u frames fps=%u cb=%u max_frame=%u\n",
	    (unsigned)player.hdr.width,
	    (unsigned)player.hdr.height,
	    (unsigned)player.hdr.frames,
	    (unsigned)player.hdr.fps,
	    (unsigned)player.hdr.cb_entries,
	    (unsigned)player.hdr.max_frame_bytes);
	printf("I/O: one fread per STFR; SND0 submitted from frame_buf into DMA ring\n");
	fflush(stdout);

	/* NULL screens => allocate private ST-RAM ping-pong (standalone). */
	if (stvq_hw_init(&hw, player.hdr.width, player.hdr.height, NULL, NULL, 1) != 0) {
		printf("error: video init failed\n");
		stvq_player_close(&player);
		fclose(fp);
		return 1;
	}
	player.hw = &hw;
	use_audio = hw.dma_ok && (player.hdr.flags & STVQ_FLAG_SOUND) != 0;
	printf("Audio: dma_ok=%d flags=0x%x -> %s\n",
	    hw.dma_ok,
	    (unsigned)player.hdr.flags,
	    use_audio ? "STE-DMA clock" : "silent VBL pace");
	fflush(stdout);

	stvq_hw_set_pending_palette(&hw, player.initial_pal);

	vbls_per_frame = player.hdr.fps ? (50u + player.hdr.fps / 2u) / player.hdr.fps : 3u;
	if (vbls_per_frame < 1)
		vbls_per_frame = 1;

	for (;;) {
		int key = stvq_hw_poll_key();
		if (key == 27) {
			rc = 0;
			break;
		}
		if (key == ' ') {
			paused = !paused;
			if (paused)
				stvq_hw_pcm_stop(&hw);
		}

		if (paused) {
			stvq_hw_wait_vbl(&hw);
			continue;
		}

		if (!have_frame) {
			int pr;
			t_frame0 = stvq_hz200();
			pr = stvq_player_next_frame(&player, &frame);
			if (pr == 0) {
				rc = 0;
				break;
			}
			if (pr < 0) {
				stvq_hw_pcm_stop(&hw);
				stvq_hw_shutdown(&hw);
				printf("error: decode failed at frame %d\n", player.frame_index);
				dump_prof(&prof);
				stvq_player_close(&player);
				fclose(fp);
				return 1;
			}
			have_frame = 1;
		}

		if (frame.have_stpl)
			stvq_hw_set_pending_palette(&hw, frame.stpl);

		{
			unsigned long tw0 = stvq_hz200();
			if (use_audio && !first) {
				/* Audio master: wait until ring has room for this frame's PCM. */
				while (stvq_hw_pcm_busy(&hw, frame.pcm_len)) {
					key = stvq_hw_poll_key();
					if (key == 27) {
						rc = 0;
						goto done;
					}
					if (key == ' ') {
						paused = 1;
						stvq_hw_pcm_stop(&hw);
						break;
					}
				}
				if (paused) {
					prof.last_wait = stvq_hz200() - tw0;
					continue;
				}
			} else if (!use_audio && !first) {
				while (vbl_accum < vbls_per_frame) {
					stvq_hw_wait_vbl(&hw);
					vbl_accum++;
					key = stvq_hw_poll_key();
					if (key == 27) {
						rc = 0;
						goto done;
					}
					if (key == ' ') {
						paused = 1;
						break;
					}
				}
				if (paused) {
					prof.last_wait = stvq_hz200() - tw0;
					continue;
				}
				vbl_accum = 0;
			}
			prof.last_wait = stvq_hz200() - tw0;
		}

		/* Submit PCM before present so the VBL wait cannot drain the ring dry. */
		if (use_audio && frame.pcm && frame.pcm_len >= 1) {
			unsigned long ta0 = stvq_hz200();
			stvq_hw_pcm_start(&hw, frame.pcm, frame.pcm_len, player.hdr.sample_rate);
			prof.last_audio = stvq_hz200() - ta0;
		}

		{
			unsigned long tp0 = stvq_hz200();
			unsigned long t_done;
			(void)stvq_hw_present(&hw);
			t_done = stvq_hz200();
			prof.last_present = t_done - tp0;
			/*
			 * Present cadence: more than one VBL past the nominal frame
			 * period means this reveal was >=1 VBL late.
			 */
			if (t_prev_present && prof.budget_ticks) {
				unsigned long dt = t_done - t_prev_present;
				if (dt > prof.budget_ticks + STVQ_HZ200_PER_VBL)
					prof.late_present++;
			}
			t_prev_present = t_done;
		}

		prof.last_total = stvq_hz200() - t_frame0;
		stvq_prof_add(&prof);

		have_frame = 0;
		first = 0;
	}

done:
	stvq_hw_pcm_stop(&hw);
	stvq_player_close(&player);
	{
		int dma_ok = hw.dma_ok;
		stvq_hw_shutdown(&hw);
		printf("Audio session: dma_ok=%d use_audio=%d\n", dma_ok, use_audio);
	}
	fclose(fp);
	dump_prof(&prof);
	return rc;
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		usage();
		return 1;
	}
	return play(argv[1]);
}
