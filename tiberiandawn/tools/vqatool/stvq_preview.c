/*
 * stvq_preview.c - Decode .stv and pipe RGB24 + s8 PCM into ffmpeg via /dev/fd/N.
 *
 * Optional stacked compose: video / palette strip / codebook matrix.
 */
#include "stvq_preview.h"

#include "stvq_c2p.h"
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

/* Compose chrome */
static const uint8_t k_black[3] = {0, 0, 0};
static const uint8_t k_yellow[3] = {255, 255, 0};

#define PAL_SWATCH_W 18u
#define PAL_SWATCH_H 8u
#define PAL_BORDER 1u
#define PAL_CELL_W (PAL_SWATCH_W + 2u * PAL_BORDER)
#define PAL_CELL_H (PAL_SWATCH_H + 2u * PAL_BORDER)
#define CB_TILE 8u
#define HIGHLIGHT_LIFE 5u /* ages 0..4; alpha → 0 at age 4 */

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

static void fill_rect(uint8_t *rgb, unsigned stride, unsigned x0, unsigned y0, unsigned w, unsigned h,
    const uint8_t color[3])
{
	unsigned y, x;
	for (y = 0; y < h; y++) {
		uint8_t *row = rgb + ((size_t)(y0 + y) * stride + x0) * 3u;
		for (x = 0; x < w; x++) {
			row[0] = color[0];
			row[1] = color[1];
			row[2] = color[2];
			row += 3;
		}
	}
}

static void blend_rect(uint8_t *rgb, unsigned stride, unsigned x0, unsigned y0, unsigned w, unsigned h,
    const uint8_t color[3], unsigned age)
{
	/* alpha = 1 - age/4 for ages 0..4 (fully transparent at frame 5 / age 4) */
	unsigned num, den = HIGHLIGHT_LIFE - 1u;
	unsigned y, x;
	if (age >= HIGHLIGHT_LIFE)
		return;
	num = den - age;
	if (!num)
		return;
	for (y = 0; y < h; y++) {
		uint8_t *row = rgb + ((size_t)(y0 + y) * stride + x0) * 3u;
		for (x = 0; x < w; x++) {
			row[0] = (uint8_t)(((unsigned)row[0] * (den - num) + (unsigned)color[0] * num) / den);
			row[1] = (uint8_t)(((unsigned)row[1] * (den - num) + (unsigned)color[1] * num) / den);
			row[2] = (uint8_t)(((unsigned)row[2] * (den - num) + (unsigned)color[2] * num) / den);
			row += 3;
		}
	}
}

/* Draw a hollow border of thickness bw inside [x0,y0,cell_w,cell_h], blending yellow. */
static void blend_border(uint8_t *rgb, unsigned stride, unsigned x0, unsigned y0, unsigned cell_w, unsigned cell_h,
    unsigned bw, unsigned age)
{
	if (!bw || age >= HIGHLIGHT_LIFE)
		return;
	if (bw * 2u > cell_w || bw * 2u > cell_h)
		bw = (cell_w < cell_h ? cell_w : cell_h) / 2u;
	if (!bw)
		return;
	/* top / bottom */
	blend_rect(rgb, stride, x0, y0, cell_w, bw, k_yellow, age);
	blend_rect(rgb, stride, x0, y0 + cell_h - bw, cell_w, bw, k_yellow, age);
	/* left / right (excluding corners already covered) */
	if (cell_h > 2u * bw) {
		blend_rect(rgb, stride, x0, y0 + bw, bw, cell_h - 2u * bw, k_yellow, age);
		blend_rect(rgb, stride, x0 + cell_w - bw, y0 + bw, bw, cell_h - 2u * bw, k_yellow, age);
	}
}

static void blit_rgb(uint8_t *dst, unsigned dst_stride, unsigned dx, unsigned dy, const uint8_t *src,
    unsigned src_w, unsigned src_h)
{
	unsigned y;
	for (y = 0; y < src_h; y++) {
		memcpy(dst + ((size_t)(dy + y) * dst_stride + dx) * 3u, src + (size_t)y * src_w * 3u,
		    (size_t)src_w * 3u);
	}
}

static void draw_palette_strip(uint8_t *rgb, unsigned stride, unsigned x0, unsigned y0, const uint16_t palette[16])
{
	unsigned i;
	for (i = 0; i < 16u; i++) {
		unsigned cx = x0 + i * PAL_CELL_W;
		uint8_t col[3];
		/* cell background = border color */
		fill_rect(rgb, stride, cx, y0, PAL_CELL_W, PAL_CELL_H, k_black);
		stvq_ste_to_rgb24(palette[i], &col[0], &col[1], &col[2]);
		fill_rect(rgb, stride, cx + PAL_BORDER, y0 + PAL_BORDER, PAL_SWATCH_W, PAL_SWATCH_H, col);
	}
}

static void draw_codebook_tile(uint8_t *rgb, unsigned stride, unsigned x0, unsigned y0, const uint8_t tile32[32],
    const uint16_t palette[16], unsigned border, unsigned highlight_age)
{
	uint8_t pens[64];
	unsigned ly, lx;
	unsigned cell = CB_TILE + 2u * border;
	unsigned tx = x0 + border;
	unsigned ty = y0 + border;

	if (border)
		fill_rect(rgb, stride, x0, y0, cell, cell, k_black);

	stvq_unpack_tile_32(tile32, pens);
	for (ly = 0; ly < CB_TILE; ly++) {
		for (lx = 0; lx < CB_TILE; lx++) {
			uint8_t r, g, b;
			uint8_t *dst = rgb + ((size_t)(ty + ly) * stride + (tx + lx)) * 3u;
			stvq_ste_to_rgb24(palette[pens[ly * CB_TILE + lx] & 15u], &r, &g, &b);
			dst[0] = r;
			dst[1] = g;
			dst[2] = b;
		}
	}

	if (highlight_age < HIGHLIGHT_LIFE) {
			if (border)
			/* yellow sits on top of the black border */
			blend_border(rgb, stride, x0, y0, cell, cell, border, highlight_age);
		else
			/* inset 1px over the tile itself */
			blend_border(rgb, stride, x0, y0, CB_TILE, CB_TILE, 1u, highlight_age);
	}
}

typedef struct ComposeGeom {
	unsigned width;
	unsigned height;
	unsigned video_y;
	unsigned pal_y;
	unsigned pal_x;
	unsigned cb_y;
	unsigned cb_x;
	unsigned cb_cols;
	unsigned cb_rows;
	unsigned cb_cell;
	unsigned cb_grid_w;
	unsigned cb_grid_h;
} ComposeGeom;

static void compose_geom(const StvqPreviewOpts *opts, const StvqPlayer *p, ComposeGeom *g)
{
	unsigned w = p->hdr.width;
	unsigned h = 0;

	memset(g, 0, sizeof(*g));
	g->width = w;
	g->cb_cell = CB_TILE + 2u * opts->cb_border;

	if (opts->show_video) {
		g->video_y = h;
		h += p->hdr.height;
	}
	if (opts->show_palette) {
		g->pal_y = h;
		g->pal_x = (w > 16u * PAL_CELL_W) ? (w - 16u * PAL_CELL_W) / 2u : 0u;
		h += PAL_CELL_H;
	}
	if (opts->show_codebook && g->cb_cell > 0 && w >= g->cb_cell) {
		g->cb_cols = w / g->cb_cell;
		g->cb_rows = (p->hdr.cb_entries + g->cb_cols - 1u) / g->cb_cols;
		g->cb_grid_w = g->cb_cols * g->cb_cell;
		g->cb_grid_h = g->cb_rows * g->cb_cell;
		g->cb_x = (w - g->cb_grid_w) / 2u;
		g->cb_y = h;
		h += g->cb_grid_h;
	}
	g->height = h ? h : 1u;
	/* libx264 yuv420p needs even dimensions */
	if (g->width & 1u)
		g->width++;
	if (g->height & 1u)
		g->height++;
}

static void compose_frame(uint8_t *out, const ComposeGeom *g, const StvqPreviewOpts *opts, const StvqPlayer *p,
    const uint8_t *hl_age)
{
	size_t nbytes = (size_t)g->width * g->height * 3u;
	memset(out, 0, nbytes);

	if (opts->show_video)
		blit_rgb(out, g->width, 0, g->video_y, p->rgb, p->hdr.width, p->hdr.height);

	if (opts->show_palette)
		draw_palette_strip(out, g->width, g->pal_x, g->pal_y, p->palette);

	if (opts->show_codebook && g->cb_cols) {
		unsigned idx;
		for (idx = 0; idx < p->hdr.cb_entries; idx++) {
			unsigned col = idx % g->cb_cols;
			unsigned row = idx / g->cb_cols;
			unsigned x = g->cb_x + col * g->cb_cell;
			unsigned y = g->cb_y + row * g->cb_cell;
			uint8_t age = hl_age ? hl_age[idx] : 0xFF;
			draw_codebook_tile(out, g->width, x, y, p->codebook + (size_t)idx * 32u, p->palette,
			    opts->cb_border, age);
		}
	}
}

static void highlight_tick(uint8_t *hl_age, unsigned n, const uint16_t *stcr_idx, unsigned stcr_n)
{
	unsigned i;
	for (i = 0; i < n; i++) {
		if (hl_age[i] < 0xFF && hl_age[i] < HIGHLIGHT_LIFE)
			hl_age[i]++;
		if (hl_age[i] >= HIGHLIGHT_LIFE)
			hl_age[i] = 0xFF;
	}
	for (i = 0; i < stcr_n; i++) {
		uint16_t idx = stcr_idx[i];
		if (idx < n)
			hl_age[idx] = 0;
	}
}

int stvq_preview(const StvqPreviewOpts *opts)
{
	StvqPlayer player;
	ComposeGeom geom;
	int vpipe[2] = {-1, -1};
	int apipe[2] = {-1, -1};
	pid_t pid = -1;
	posix_spawn_file_actions_t actions;
	char *argv[48];
	int argc = 0;
	char geom_s[64], rate[32], ar[32], vfd[64], afd[64];
	const char *ffmpeg;
	char out_path[1024];
	const char *outp;
	int rc = -1;
	int status;
	int actions_inited = 0;
	unsigned frame_i = 0;
	int compose;
	uint8_t *frame_rgb = NULL;
	uint8_t *hl_age = NULL;
	size_t frame_bytes = 0;

	memset(&player, 0, sizeof(player));
	if (!opts || !opts->stv_path) {
		fprintf(stderr, "error: preview requires an .stv path\n");
		return -1;
	}
	if (!opts->show_video && !opts->show_palette && !opts->show_codebook) {
		fprintf(stderr, "error: nothing to preview (--no-video without --palette/--codebook)\n");
		return -1;
	}

	compose = opts->show_palette || opts->show_codebook || !opts->show_video;
	ffmpeg = resolve_ffmpeg(opts->ffmpeg);
	outp = opts->out_path;
	if (!outp) {
		default_out(opts->stv_path, out_path, sizeof(out_path));
		outp = out_path;
	}

	if (stvq_player_open(&player, opts->stv_path) != 0)
		return -1;

	compose_geom(opts, &player, &geom);
	if (compose) {
		frame_bytes = (size_t)geom.width * geom.height * 3u;
		frame_rgb = (uint8_t *)malloc(frame_bytes);
		if (!frame_rgb)
			goto done;
		if (opts->show_codebook) {
			hl_age = (uint8_t *)malloc(player.hdr.cb_entries);
			if (!hl_age)
				goto done;
			memset(hl_age, 0xFF, player.hdr.cb_entries);
		}
	} else {
		geom.width = player.hdr.width;
		geom.height = player.hdr.height;
	}

	if (pipe(vpipe) != 0 || pipe(apipe) != 0) {
		fprintf(stderr, "error: pipe: %s\n", strerror(errno));
		goto done;
	}

	/* Keep read ends open across exec */
	fcntl(vpipe[0], F_SETFD, 0);
	fcntl(apipe[0], F_SETFD, 0);

	snprintf(geom_s, sizeof(geom_s), "%ux%u", geom.width, geom.height);
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
	argv[argc++] = geom_s;
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

	fprintf(stderr, "ffmpeg: %s → %s (%s @ %s fps, %s Hz, h264/aac)\n", ffmpeg, outp, geom_s, rate, ar);
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
		const uint8_t *rgb_src;
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

		if (compose) {
			if (hl_age)
				highlight_tick(hl_age, player.hdr.cb_entries, player.stcr_idx, player.stcr_n);
			compose_frame(frame_rgb, &geom, opts, &player, hl_age);
			rgb_src = frame_rgb;
			rgb_n = frame_bytes;
		} else {
			rgb_src = player.rgb;
			rgb_n = (size_t)player.hdr.width * player.hdr.height * 3u;
		}
		if (write_all(vpipe[1], rgb_src, rgb_n) != 0) {
			fprintf(stderr, "error: video pipe write failed\n");
			goto done;
		}
		frame_i++;
		if ((frame_i % 50) == 0)
			fprintf(stderr, "  frame %u\n", frame_i);
	}

	rc = 0;

done:
	free(frame_rgb);
	free(hl_age);
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
