/*
 * stvq_game.cpp - Play FORM STVQ from a CCFileClass mix member (game screens + Digi HAL).
 */
#ifdef ATARI_ST

#include "stvq_game.h"

#include "function.h"
#include "audio.h"
#include "st_screen.h"
#include "stvq_format.h"
#include "stvq_hw.h"
#include "stvq_io.h"
#include "stvq_player.h"

#include <string.h>

extern bool InMovie;

static size_t stvq_ccfile_read(void* user, void* buf, size_t n)
{
	CCFileClass* f = (CCFileClass*)user;
	int r = f->Read(buf, (int)n);
	if (r < 0) {
		return 0;
	}
	return (size_t)r;
}

static int stvq_ccfile_seek(void* user, long off, int whence)
{
	CCFileClass* f = (CCFileClass*)user;
	if (f->Seek((int)off, whence) < 0) {
		return -1;
	}
	return 0;
}

static int stvq_poll_esc_break(void)
{
	if (!Keyboard->Check()) {
		return 0;
	}
	int key = Keyboard->Get();
	Keyboard->Clear();
	if ((BreakoutAllowed || Debug_Flag) && key == KN_ESC) {
		Brokeout = true;
		return 1;
	}
	return 0;
}

static int stvq_play_movie_file(CCFileClass& file, int use_audio)
{
	StvqIo io;
	StvqHw hw;
	StvqPlayer player;
	StvqFrame frame;
	uint8_t* visible0 = (uint8_t*)VisiblePage.Get_Buffer();
	uint8_t* hidden0 = (uint8_t*)ST_Screen_Backplane_Page();
	int yielded = 0;
	int first = 1;
	unsigned vbl_accum = 0;
	unsigned vbls_per_frame;
	int rc = 0;

	memset(&io, 0, sizeof(io));
	memset(&hw, 0, sizeof(hw));
	memset(&player, 0, sizeof(player));
	memset(&frame, 0, sizeof(frame));

	io.user = &file;
	io.read = stvq_ccfile_read;
	io.seek = stvq_ccfile_seek;

	if (stvq_player_open(&player, &hw, &io) != 0) {
		DBG_INFO("STVQ: open/init failed (%s)",
		    stvq_player_open_error ? stvq_player_open_error : "?");
#ifdef CHEAT_KEYS
		Mono_Printf("STVQ open fail: %s\n", stvq_player_open_error ? stvq_player_open_error : "?");
#endif
		return -1;
	}

	if (use_audio) {
		Ste_Audio_Yield_Dma();
		yielded = 1;
	}

	if (stvq_hw_init(&hw, player.hdr.width, player.hdr.height, visible0, hidden0, use_audio) != 0) {
		DBG_INFO("STVQ: video init failed (OOM?)");
		stvq_player_close(&player);
		if (yielded) {
			Ste_Audio_Reclaim_Dma();
		}
		return -1;
	}
	player.hw = &hw;

	/* Digi path only if HAL Submit is live. */
	use_audio = use_audio && hw.dma_ok;

	stvq_hw_set_pending_palette(&hw, player.initial_pal);
	vbls_per_frame = player.hdr.fps ? (50u + player.hdr.fps / 2u) / player.hdr.fps : 3u;
	if (vbls_per_frame < 1) {
		vbls_per_frame = 1;
	}

	Brokeout = false;
	InMovie = true;

	for (;;) {
		if (stvq_poll_esc_break()) {
			rc = 1;
			break;
		}

		{
			int pr = stvq_player_read_frame(&player);
			if (pr == 0) {
				break;
			}
			if (pr < 0) {
				CCDebugString("STVQ: read failed\n");
				rc = -1;
				goto done;
			}
			pr = stvq_player_decode_frame(&player, &frame);
			if (pr == 0) {
				break;
			}
			if (pr < 0) {
				CCDebugString("STVQ: decode failed\n");
				rc = -1;
				goto done;
			}
		}

		if (!use_audio && !first) {
			while (vbl_accum < vbls_per_frame) {
				stvq_hw_wait_vbl(&hw);
				vbl_accum++;
				if (stvq_poll_esc_break()) {
					rc = 1;
					goto done;
				}
			}
			vbl_accum = 0;
		}

		/* After pacing waits: mutating pending_pal before wait would race TOS colorptr. */
		if (frame.have_stpl) {
			stvq_hw_set_pending_palette(&hw, frame.stpl);
		}

		if (use_audio && frame.pcm && frame.pcm_len >= 1) {
			const unsigned char* s = frame.pcm;
			size_t left = frame.pcm_len;

			while (left > 0) {
				unsigned got = stvq_hw_pcm_write(&hw, s, (unsigned)left, player.hdr.sample_rate);
				if (got == 0) {
					if (stvq_poll_esc_break()) {
						rc = 1;
						goto done;
					}
					continue;
				}
				s += got;
				left -= got;
			}
		}

		/* Queue flip, then prefetch next STFR during the present VBL wait. */
		stvq_hw_present_begin(&hw);
		{
			int pr = stvq_player_read_frame(&player);
			if (pr < 0) {
				CCDebugString("STVQ: prefetch read failed\n");
				rc = -1;
				stvq_hw_present_end(&hw);
				goto done;
			}
		}
		stvq_hw_present_end(&hw);
		first = 0;
	}

done:
	stvq_hw_pcm_stop(&hw);
	stvq_player_close(&player);
	stvq_hw_shutdown(&hw);
	/* Restore game visible plane (Logbase/phys) after Setscreen flips during play. */
	ST_Screen_Apply_Game_Video_Hardware();
	if (yielded) {
		Ste_Audio_Reclaim_Dma();
	}
	InMovie = false;
	if (rc < 0) {
		DBG_INFO("STVQ: play aborted with error");
	} else if (rc > 0) {
		DBG_INFO("STVQ: play skipped (ESC)");
	} else {
		DBG_INFO("STVQ: play finished OK");
	}
	return rc;
}

int Stvq_Play_Named_Movie(char const* fullname, int use_audio)
{
	CCFileClass file(fullname);
	unsigned char peek[12];

	if (!file.Is_Available() || !file.Open(READ)) {
		DBG_INFO("STVQ: skip %s (missing / unavailable)", fullname);
		return 0;
	}
	if (file.Read(peek, 12) != 12) {
		file.Close();
		DBG_INFO("STVQ: skip %s (short header read)", fullname);
		return 0;
	}

	uint32_t form_id = stvq_read_be32(peek);
	uint32_t type_id = stvq_read_be32(peek + 8);
	file.Seek(0, SEEK_SET);

	if (form_id != STVQ_CHUNK_FORM || type_id != STVQ_CHUNK_STVQ) {
		DBG_INFO("STVQ: skip %s (not FORM STVQ)", fullname);
#ifdef CHEAT_KEYS
		Mono_Printf("STVQ skip non-STV [%s]\n", fullname);
#endif
		file.Close();
		return 0;
	}

	int play_rc = stvq_play_movie_file(file, use_audio);
	file.Close();
	if (play_rc > 0 || Brokeout) {
		Brokeout = false;
		return 1;
	}
	return 0;
}

#endif /* ATARI_ST */
