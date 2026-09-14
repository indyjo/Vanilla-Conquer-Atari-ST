/*
 * stvqview.c - Standalone FORM 'STVQ' player for Atari ST/STE.
 *
 * Usage: stvqview.ttp file.stv
 * Keys: ESC quit, Space pause/resume.
 *
 * Thin CLI over atarilib/stvq (FILE* StvqIo adapter; private screens).
 * Play runs under Supexec (hardware paths expect supervisor).
 */
#include "stvq_format.h"
#include "stvq_hw.h"
#include "stvq_io.h"
#include "stvq_player.h"

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

static int play(const char *path)
{
	FILE *fp;
	StvqIo io;
	StvqHw hw;
	StvqPlayer player;
	StvqFrame frame;
	int paused = 0;
	int first = 1;
	int use_audio;
	unsigned vbl_accum = 0;
	unsigned vbls_per_frame;
	int rc = 0;
	int key;
	int pr;
	int dma_ok;
	const char *err = NULL;

	memset(&io, 0, sizeof(io));
	memset(&hw, 0, sizeof(hw));
	memset(&player, 0, sizeof(player));
	memset(&frame, 0, sizeof(frame));

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

	printf("STVQ %u x %u %u frames fps=%u cb=%u max_frame=%u\n",
	    (unsigned)player.hdr.width,
	    (unsigned)player.hdr.height,
	    (unsigned)player.hdr.frames,
	    (unsigned)player.hdr.fps,
	    (unsigned)player.hdr.cb_entries,
	    (unsigned)player.hdr.max_frame_bytes);
	printf("I/O: prefetch next STFR during present VBL; SND0 via Digi_Submit\n");
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
	printf("Audio: digi_ok=%d flags=0x%x -> %s\n",
	    hw.dma_ok,
	    (unsigned)player.hdr.flags,
	    use_audio ? "Digi_Submit clock" : "silent VBL pace (need Digi HAL)");
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

		pr = stvq_player_read_frame(&player);
		if (pr == 0)
			break;
		if (pr < 0) {
			err = "frame failed";
			rc = 1;
			goto done;
		}
		pr = stvq_player_decode_frame(&player, &frame);
		if (pr == 0)
			break;
		if (pr < 0) {
			err = "frame failed";
			rc = 1;
			goto done;
		}

		if (!use_audio && !first) {
			while (vbl_accum < vbls_per_frame) {
				stvq_hw_wait_vbl(&hw);
				vbl_accum++;
				if (stvq_hw_poll_key() == 27)
					goto done;
			}
			vbl_accum = 0;
		}

		/* After pacing waits: mutating pending_pal before wait would race TOS colorptr. */
		if (frame.have_stpl)
			stvq_hw_set_pending_palette(&hw, frame.stpl);

		/* Submit PCM before present so the VBL wait cannot drain the ring dry.
		 * Write whatever fits immediately; retry until the whole SND0 is queued. */
		if (use_audio && frame.pcm && frame.pcm_len >= 1) {
			const unsigned char *s = frame.pcm;
			size_t left = frame.pcm_len;

			while (left > 0) {
				unsigned got = stvq_hw_pcm_write(&hw, s, (unsigned)left, player.hdr.sample_rate);
				if (got == 0) {
					if (stvq_hw_poll_key() == 27)
						goto done;
					continue;
				}
				s += got;
				left -= got;
			}
		}

		/*
		 * Queue flip, then read the next STFR before Vsync so disk time
		 * collapses into the present VBL wait when the read is short enough.
		 */
		stvq_hw_present_begin(&hw);
		pr = stvq_player_read_frame(&player);
		stvq_hw_present_end(&hw);
		if (pr < 0) {
			err = "prefetch read failed";
			rc = 1;
			goto done;
		}

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
