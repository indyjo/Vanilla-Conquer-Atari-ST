/*
 * stvq_refine_w16.c - Refine name.<N>.w16 via palette-opt (one invoke per segment).
 * Creates missing .pal/.hist; uses -c only when .w16 already exists.
 */
#include "stvq_refine_w16.h"

#include "stvq_palette.h"
#include "vqa_decode.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_EXTRA_ARGS 64

static int file_exists(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* Split comma-separated list into argv pieces; mutates buf. Returns count. */
static int split_comma_args(char *buf, char **argv, int max_argv)
{
	int n = 0;
	char *p = buf;
	while (p && *p && n < max_argv) {
		char *comma = strchr(p, ',');
		if (comma)
			*comma = '\0';
		if (*p)
			argv[n++] = p;
		if (!comma)
			break;
		p = comma + 1;
	}
	return n;
}

static int run_palette_opt(const StvqRefineW16Opts *opts, int continue_mode, const char *pal, const char *hist,
    const char *w16, char **extra, int n_extra)
{
	char *argv[16 + MAX_EXTRA_ARGS];
	int argc = 0;
	int i;
	pid_t pid;
	int status;

	argv[argc++] = (char *)opts->palette_opt;
	if (continue_mode)
		argv[argc++] = "-c";
	argv[argc++] = "-p";
	argv[argc++] = (char *)pal;
	argv[argc++] = "-o";
	argv[argc++] = (char *)w16;
	argv[argc++] = "--hist";
	argv[argc++] = (char *)hist;
	argv[argc++] = "--fix=0,0";
	if (opts->mode == STVQ_REFINE_NORMAL)
		argv[argc++] = "--sa-t0=1e-4";
	else if (opts->mode == STVQ_REFINE_QUICK)
		argv[argc++] = "--sa-t0=1.5e-5";
	for (i = 0; i < n_extra; i++)
		argv[argc++] = extra[i];
	argv[argc] = NULL;

	if (opts->dry_run) {
		fprintf(stderr, "dry-run:");
		for (i = 0; argv[i]; i++)
			fprintf(stderr, " %s", argv[i]);
		fprintf(stderr, "\n");
		return 0;
	}

	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "error: fork: %s\n", strerror(errno));
		return -1;
	}
	if (pid == 0) {
		execv(opts->palette_opt, argv);
		fprintf(stderr, "error: execv %s: %s\n", opts->palette_opt, strerror(errno));
		_exit(127);
	}
	if (waitpid(pid, &status, 0) < 0)
		return -1;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		fprintf(stderr, "error: palette-opt failed for %s (status %d)\n", w16,
		    WIFEXITED(status) ? WEXITSTATUS(status) : -1);
		return -1;
	}
	return 0;
}

static int write_seg_pal_hist(const VqaDecode *dec, int seg, const char *pal_path, const char *hist_path)
{
	const VqaPalSegment *seginfo = &dec->segments[seg];
	uint64_t counts[256];
	unsigned f;

	memset(counts, 0, sizeof(counts));
	for (f = (unsigned)seginfo->start_frame; f <= (unsigned)seginfo->end_frame && f < dec->frame_count; f++) {
		const unsigned char *px = dec->frames[f].pixels;
		size_t n = (size_t)dec->width * dec->height;
		size_t i;
		for (i = 0; i < n; i++)
			counts[px[i]]++;
	}
	if (stvq_write_pal(pal_path, seginfo->pal) != 0)
		return -1;
	if (stvq_write_hist(hist_path, counts) != 0)
		return -1;
	return 0;
}

int stvq_refine_w16(const StvqRefineW16Opts *opts)
{
	VqaDecode dec;
	char *extra_buf = NULL;
	char *extra[MAX_EXTRA_ARGS];
	int n_extra = 0;
	unsigned s;
	int rc = -1;

	memset(&dec, 0, sizeof(dec));
	if (!opts || !opts->vqa_path || !opts->palette_opt || !opts->palette_opt[0]) {
		fprintf(stderr, "error: refine-w16 requires VQA path and --palette-opt\n");
		return -1;
	}

	if (opts->palette_opt_args && opts->palette_opt_args[0]) {
		extra_buf = strdup(opts->palette_opt_args);
		if (!extra_buf)
			return -1;
		n_extra = split_comma_args(extra_buf, extra, MAX_EXTRA_ARGS);
		if (n_extra >= MAX_EXTRA_ARGS && strchr(opts->palette_opt_args, ','))
			fprintf(stderr, "warning: --palette-opt-args truncated at %d tokens\n", MAX_EXTRA_ARGS);
	}

	fprintf(stderr, "decoding %s...\n", opts->vqa_path);
	if (vqa_decode_file(opts->vqa_path, &dec) != 0) {
		fprintf(stderr, "error: VQA decode failed\n");
		free(extra_buf);
		return -1;
	}

	fprintf(stderr, "refine-w16: %u segment(s), mode=%s%s\n", dec.segment_count,
	    opts->mode == STVQ_REFINE_QUICK         ? "quick"
	        : opts->mode == STVQ_REFINE_THOROUGH ? "thorough"
	                                             : "normal",
	    opts->dry_run ? " (dry-run)" : "");

	for (s = 0; s < dec.segment_count; s++) {
		char pal[768], hist[768], w16[768];
		int cont;

		stvq_sidecar_paths(opts->vqa_path, (int)s, pal, hist, w16, sizeof(pal));
		cont = file_exists(w16);

		if (opts->dry_run) {
			fprintf(stderr, "  seg %u frames %d..%d → %s%s\n", s, dec.segments[s].start_frame,
			    dec.segments[s].end_frame, w16, cont ? " (-c)" : " (new)");
		} else {
			if (write_seg_pal_hist(&dec, (int)s, pal, hist) != 0)
				goto done;
		}

		fprintf(stderr, "  %s %s...\n", cont ? "refining" : "creating", w16);
		if (run_palette_opt(opts, cont, pal, hist, w16, extra, n_extra) != 0)
			goto done;
	}

	rc = 0;
done:
	free(extra_buf);
	vqa_decode_free(&dec);
	return rc;
}
