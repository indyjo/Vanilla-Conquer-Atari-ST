/*
 * stvqview.c - Standalone FORM 'STVQ' player for Atari ST/STE.
 *
 * Usage: stvqview.ttp file.stv
 * Keys: ESC quit, Space pause/resume.
 * Writes STVQPROF.TXT with 200Hz timing on exit.
 *
 * Thin CLI over atarilib/stvq (FILE* StvqIo adapter; private screens).
 * Play runs under Supexec (hardware paths expect supervisor).
 */
#include "stvq_format.h"
#include "stvq_hw.h"
#include "stvq_io.h"
#include "stvq_player.h"
#include "stvq_prof.h"

#include <mint/osbind.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

static size_t file_read(void *user, void *buf, size_t n)
{
	FILE *fp = (FILE *)user;
	return fread(buf, 1, n, fp);
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
	int use_audio;
	unsigned vbl_accum = 0;
	unsigned vbls_per_frame;
	int rc = 0;
	int key;
	int pr;
	int dma_ok;
	unsigned fps;
	unsigned long t_frame0 = 0;
	unsigned long t_prev_present = 0;
	unsigned long tw0;
	unsigned long ta0;
	unsigned long tp0;
	unsigned long t_done;
	unsigned long dt;
	const char *err = NULL;

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
	fps = player.hdr.fps ? player.hdr.fps : 15u;
	prof.budget_ticks = (STVQ_HZ200_PER_SEC + fps / 2u) / fps;
	if (!prof.budget_ticks)
		prof.budget_ticks = 1;

	printf("STVQ %u x %u %u frames fps=%u cb=%u max_frame=%u\n",
	    (unsigned)player.hdr.width,
	    (unsigned)player.hdr.height,
	    (unsigned)player.hdr.frames,
	    (unsigned)player.hdr.fps,
	    (unsigned)player.hdr.cb_entries,
	    (unsigned)player.hdr.max_frame_bytes);
	printf("I/O: prefetch next STFR during present VBL; SND0 into DMA ring\n");
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
		key = stvq_hw_poll_key();
		if (key == 27)
			break;
		if (key == ' ') {
			paused = !paused;
			if (paused)
				stvq_hw_pcm_stop(&hw);
		}
		if (paused) {
			stvq_hw_wait_vbl(&hw);
			continue;
		}

		t_frame0 = stvq_hz200();
		pr = stvq_player_next_frame(&player, &frame);
		if (pr == 0)
			break;
		if (pr < 0) {
			err = "frame failed";
			rc = 1;
			goto done;
		}

		tw0 = stvq_hz200();
		if (use_audio && !first) {
			while (stvq_hw_pcm_busy(&hw, frame.pcm_len)) {
				if (stvq_hw_poll_key() == 27)
					goto done;
			}
		} else if (!use_audio && !first) {
			while (vbl_accum < vbls_per_frame) {
				stvq_hw_wait_vbl(&hw);
				vbl_accum++;
				if (stvq_hw_poll_key() == 27)
					goto done;
			}
			vbl_accum = 0;
		}
		prof.last_wait = stvq_hz200() - tw0;

		/* After pacing waits: mutating pending_pal before wait would race TOS colorptr. */
		if (frame.have_stpl)
			stvq_hw_set_pending_palette(&hw, frame.stpl);

		/* Submit PCM before present so the VBL wait cannot drain the ring dry. */
		if (use_audio && frame.pcm && frame.pcm_len >= 1) {
			ta0 = stvq_hz200();
			stvq_hw_pcm_start(&hw, frame.pcm, frame.pcm_len, player.hdr.sample_rate);
			prof.last_audio = stvq_hz200() - ta0;
		}

		tp0 = stvq_hz200();
		/*
		 * Queue flip, then read the next STFR before Vsync so disk time
		 * collapses into the present VBL wait when the read is short enough.
		 */
		stvq_hw_present_begin(&hw);
		pr = stvq_player_read_frame(&player);
		(void)stvq_hw_present_end(&hw);
		t_done = stvq_hz200();
		prof.last_present = t_done - tp0;
		if (pr < 0) {
			err = "prefetch read failed";
			rc = 1;
			goto done;
		}

		if (t_prev_present && prof.budget_ticks) {
			dt = t_done - t_prev_present;
			if (dt > prof.budget_ticks + STVQ_HZ200_PER_VBL)
				prof.late_present++;
		}
		t_prev_present = t_done;

		prof.last_total = stvq_hz200() - t_frame0;
		stvq_prof_add(&prof);
		first = 0;
	}

done:
	if (err)
		printf("error: %s at frame %d\n", err, player.frame_index);
	stvq_hw_pcm_stop(&hw);
	stvq_player_close(&player);
	dma_ok = hw.dma_ok;
	stvq_hw_shutdown(&hw);
	printf("Audio session: dma_ok=%d use_audio=%d\n", dma_ok, use_audio);
	fclose(fp);
	dump_prof(&prof);
	return rc;
}

/* Supexec entry: filename via g_play_path (Supexec callbacks take no args). */
static const char *g_play_path;

static long play_super(void)
{
	return (long)play(g_play_path);
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		usage();
		return 1;
	}
	g_play_path = argv[1];
	return (int)Supexec(play_super);
}
