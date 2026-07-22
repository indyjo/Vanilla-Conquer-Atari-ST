/*
 * stvq_init_w16.c - Write .pal/.hist and create missing .w16 via palette-opt.
 */
#include "stvq_init_w16.h"

#include "stvq_palette.h"
#include "vqa_decode.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int file_exists(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int run_palette_opt(const char *palette_opt, const char *pal, const char *hist, const char *w16)
{
	pid_t pid = fork();
	int status;
	if (pid < 0) {
		fprintf(stderr, "error: fork: %s\n", strerror(errno));
		return -1;
	}
	if (pid == 0) {
		execl(palette_opt, palette_opt, "-p", pal, "-o", w16, "--hist", hist, "--sa-iter=0",
		    "--fix=0,0", (char *)NULL);
		fprintf(stderr, "error: execl %s: %s\n", palette_opt, strerror(errno));
		_exit(127);
	}
	if (waitpid(pid, &status, 0) < 0)
		return -1;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		fprintf(stderr, "error: palette-opt failed for %s\n", w16);
		return -1;
	}
	return 0;
}

static int init_segment(const StvqInitW16Opts *opts, const VqaDecode *dec, int seg)
{
	const VqaPalSegment *seginfo = &dec->segments[seg];
	char pal_path[768], hist_path[768], w16_path[768];
	uint64_t counts[256];
	unsigned f;

	memset(counts, 0, sizeof(counts));
	stvq_sidecar_paths(opts->vqa_path, seg, pal_path, hist_path, w16_path, sizeof(pal_path));

	for (f = (unsigned)seginfo->start_frame; f <= (unsigned)seginfo->end_frame && f < dec->frame_count; f++) {
		const unsigned char *px = dec->frames[f].pixels;
		size_t n = (size_t)dec->width * dec->height;
		size_t i;
		for (i = 0; i < n; i++)
			counts[px[i]]++;
	}

	if (opts->dry_run) {
		fprintf(stderr, "  seg %d frames %d..%d → %s / %s / %s%s\n", seg, seginfo->start_frame,
		    seginfo->end_frame, pal_path, hist_path, w16_path,
		    file_exists(w16_path) ? " (w16 exists)" : " (will create w16)");
		return 0;
	}

	if (stvq_write_pal(pal_path, seginfo->pal) != 0)
		return -1;
	if (stvq_write_hist(hist_path, counts) != 0)
		return -1;

	if (file_exists(w16_path)) {
		fprintf(stderr, "  keep existing %s\n", w16_path);
		return 0;
	}

	fprintf(stderr, "  palette-opt: creating %s\n", w16_path);
	return run_palette_opt(opts->palette_opt, pal_path, hist_path, w16_path);
}

int stvq_init_w16(const StvqInitW16Opts *opts)
{
	VqaDecode dec;
	unsigned s;
	int rc = -1;

	memset(&dec, 0, sizeof(dec));
	if (!opts || !opts->vqa_path || !opts->palette_opt || !opts->palette_opt[0]) {
		fprintf(stderr, "error: init-w16 requires VQA path and --palette-opt\n");
		return -1;
	}

	fprintf(stderr, "decoding %s...\n", opts->vqa_path);
	if (vqa_decode_file(opts->vqa_path, &dec) != 0) {
		fprintf(stderr, "error: VQA decode failed\n");
		return -1;
	}

	fprintf(stderr, "init-w16: %u segment(s)%s\n", dec.segment_count, opts->dry_run ? " (dry-run)" : "");
	for (s = 0; s < dec.segment_count; s++) {
		if (init_segment(opts, &dec, (int)s) != 0)
			goto done;
	}

	rc = 0;
done:
	vqa_decode_free(&dec);
	return rc;
}
