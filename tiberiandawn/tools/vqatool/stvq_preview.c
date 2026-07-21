/*
 * stvq_preview.c - Decode .stv and pipe RGB24 + s8 PCM into ffmpeg via /dev/fd/N.
 */
#include "stvq_preview.h"

#include "stvq_play.h"

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static const char *resolve_ffmpeg(const char *explicit_path)
{
	const char *env;
	if (explicit_path && explicit_path[0])
		return explicit_path;
	env = getenv("FFMPEG");
	if (env && env[0])
		return env;
	return "ffmpeg";
}

static void default_out(const char *stv, char *out, size_t n)
{
	size_t len;
	char *dot;
	snprintf(out, n, "%s", stv);
	len = strlen(out);
	dot = strrchr(out, '.');
	if (dot && (size_t)(dot - out) + 5 < n)
		memcpy(dot, ".mkv", 5);
	else if (len + 4 < n)
		memcpy(out + len, ".mkv", 5);
}

static int write_all(int fd, const void *buf, size_t n)
{
	const unsigned char *p = (const unsigned char *)buf;
	size_t off = 0;
	while (off < n) {
		ssize_t w = write(fd, p + off, n - off);
		if (w < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (w == 0)
			return -1;
		off += (size_t)w;
	}
	return 0;
}

/* Samples that should accompany frame_index at hdr sample_rate / fps. */
static unsigned frame_audio_samples(unsigned sample_rate, unsigned fps, unsigned frame_index)
{
	uint64_t t0, t1;
	if (!fps)
		fps = 15;
	t0 = ((uint64_t)frame_index * sample_rate + fps / 2u) / fps;
	t1 = ((uint64_t)(frame_index + 1u) * sample_rate + fps / 2u) / fps;
	return (unsigned)(t1 - t0);
}

static int write_pcm_s16(int fd, const uint8_t *s8, size_t n)
{
	size_t i;
	int16_t *s16 = (int16_t *)malloc(n * sizeof(int16_t));
	if (!s16)
		return -1;
	for (i = 0; i < n; i++) {
		int8_t s = (int8_t)s8[i];
		s16[i] = (int16_t)(s << 8);
	}
	if (write_all(fd, s16, n * sizeof(int16_t)) != 0) {
		free(s16);
		return -1;
	}
	free(s16);
	return 0;
}

static int write_silence_s16(int fd, size_t n)
{
	int16_t *s16 = (int16_t *)calloc(n, sizeof(int16_t));
	int rc;
	if (!s16)
		return -1;
	rc = write_all(fd, s16, n * sizeof(int16_t));
	free(s16);
	return rc;
}

int stvq_preview(const StvqPreviewOpts *opts)
{
	StvqPlayer player;
	int vpipe[2] = {-1, -1};
	int apipe[2] = {-1, -1};
	pid_t pid = -1;
	posix_spawn_file_actions_t actions;
	char *argv[48];
	int argc = 0;
	char geom[64], rate[32], ar[32], vfd[64], afd[64];
	const char *ffmpeg;
	char out_path[1024];
	const char *outp;
	int rc = -1;
	int status;
	int actions_inited = 0;
	unsigned frame_i = 0;

	memset(&player, 0, sizeof(player));
	if (!opts || !opts->stv_path) {
		fprintf(stderr, "error: preview requires an .stv path\n");
		return -1;
	}

	ffmpeg = resolve_ffmpeg(opts->ffmpeg);
	outp = opts->out_path;
	if (!outp) {
		default_out(opts->stv_path, out_path, sizeof(out_path));
		outp = out_path;
	}

	if (stvq_player_open(&player, opts->stv_path) != 0)
		return -1;

	if (pipe(vpipe) != 0 || pipe(apipe) != 0) {
		fprintf(stderr, "error: pipe: %s\n", strerror(errno));
		goto done;
	}

	/* Keep read ends open across exec */
	fcntl(vpipe[0], F_SETFD, 0);
	fcntl(apipe[0], F_SETFD, 0);

	snprintf(geom, sizeof(geom), "%ux%u", player.hdr.width, player.hdr.height);
	snprintf(rate, sizeof(rate), "%u", (unsigned)player.hdr.fps);
	snprintf(ar, sizeof(ar), "%u", (unsigned)player.hdr.sample_rate);
	snprintf(vfd, sizeof(vfd), "/dev/fd/%d", vpipe[0]);
	snprintf(afd, sizeof(afd), "/dev/fd/%d", apipe[0]);

	/*
	 * H.264 + AAC is far more seekable in VLC than FFV1 + raw PCM from a pipe
	 * (missing cues / huge intra frames). Fall back to mpeg4 if needed at runtime
	 * is left to the user via a custom ffmpeg wrapper; libx264 is the default.
	 */
	argv[argc++] = (char *)ffmpeg;
	argv[argc++] = "-hide_banner";
	argv[argc++] = "-loglevel";
	argv[argc++] = "error";
	argv[argc++] = "-y";
	argv[argc++] = "-fflags";
	argv[argc++] = "+genpts";
	argv[argc++] = "-f";
	argv[argc++] = "rawvideo";
	argv[argc++] = "-pix_fmt";
	argv[argc++] = "rgb24";
	argv[argc++] = "-s";
	argv[argc++] = geom;
	argv[argc++] = "-r";
	argv[argc++] = rate;
	argv[argc++] = "-i";
	argv[argc++] = vfd;
	argv[argc++] = "-f";
	argv[argc++] = "s16le";
	argv[argc++] = "-ar";
	argv[argc++] = ar;
	argv[argc++] = "-ac";
	argv[argc++] = "1";
	argv[argc++] = "-i";
	argv[argc++] = afd;
	argv[argc++] = "-c:v";
	argv[argc++] = "libx264";
	argv[argc++] = "-preset";
	argv[argc++] = "veryfast";
	argv[argc++] = "-crf";
	argv[argc++] = "18";
	argv[argc++] = "-pix_fmt";
	argv[argc++] = "yuv420p";
	argv[argc++] = "-g";
	argv[argc++] = rate; /* keyframe every 1s for cheap seeks */
	argv[argc++] = "-c:a";
	argv[argc++] = "aac";
	argv[argc++] = "-b:a";
	argv[argc++] = "128k";
	argv[argc++] = "-movflags";
	argv[argc++] = "+faststart";
	argv[argc++] = (char *)outp;
	argv[argc] = NULL;

	if (posix_spawn_file_actions_init(&actions) != 0)
		goto done;
	actions_inited = 1;
	posix_spawn_file_actions_addclose(&actions, vpipe[1]);
	posix_spawn_file_actions_addclose(&actions, apipe[1]);

	fprintf(stderr, "ffmpeg: %s → %s (%s @ %s fps, %s Hz, h264/aac)\n", ffmpeg, outp, geom, rate, ar);
	if (posix_spawnp(&pid, ffmpeg, &actions, NULL, argv, environ) != 0) {
		fprintf(stderr, "error: posix_spawnp %s: %s\n", ffmpeg, strerror(errno));
		pid = -1;
		goto done;
	}

	close(vpipe[0]);
	vpipe[0] = -1;
	close(apipe[0]);
	apipe[0] = -1;

	for (;;) {
		size_t rgb_n;
		unsigned expect;
		int pr = stvq_player_next_frame(&player);
		if (pr == 0)
			break;
		if (pr < 0)
			goto done;

		expect = frame_audio_samples(player.hdr.sample_rate, player.hdr.fps, frame_i);
		if (expect) {
			size_t take = player.pcm_len < expect ? player.pcm_len : (size_t)expect;
			if (take && write_pcm_s16(apipe[1], player.pcm, take) != 0) {
				fprintf(stderr, "error: audio pipe write failed\n");
				goto done;
			}
			if (take < expect && write_silence_s16(apipe[1], expect - take) != 0) {
				fprintf(stderr, "error: audio pad write failed\n");
				goto done;
			}
		}

		rgb_n = (size_t)player.hdr.width * player.hdr.height * 3u;
		if (write_all(vpipe[1], player.rgb, rgb_n) != 0) {
			fprintf(stderr, "error: video pipe write failed\n");
			goto done;
		}
		frame_i++;
		if ((frame_i % 50) == 0)
			fprintf(stderr, "  frame %u\n", frame_i);
	}

	rc = 0;

done:
	if (vpipe[1] >= 0)
		close(vpipe[1]);
	if (apipe[1] >= 0)
		close(apipe[1]);
	if (vpipe[0] >= 0)
		close(vpipe[0]);
	if (apipe[0] >= 0)
		close(apipe[0]);
	if (actions_inited)
		posix_spawn_file_actions_destroy(&actions);

	if (pid > 0) {
		if (waitpid(pid, &status, 0) < 0) {
			rc = -1;
		} else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			fprintf(stderr, "error: ffmpeg exited with status %d\n",
			    WIFEXITED(status) ? WEXITSTATUS(status) : -1);
			rc = -1;
		} else if (rc == 0) {
			fprintf(stderr, "wrote %s (%u frames)\n", outp, frame_i);
		}
	}

	stvq_player_close(&player);
	return rc;
}
