/*
 * stvq_refine_w16.c - Enumerate name.<N>.w16 and continue-refine with palette-opt.
 */
#include "stvq_refine_w16.h"

#include "stvq_palette.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_EXTRA_ARGS 64
#define MAX_SEGMENTS 256

static void stem_from_vqa(const char *vqa_path, char *stem, size_t n)
{
	const char *base = strrchr(vqa_path, '/');
	char *dot;
	base = base ? base + 1 : vqa_path;
	snprintf(stem, n, "%s", base);
	dot = strrchr(stem, '.');
	if (dot)
		*dot = '\0';
}

static void dir_from_vqa(const char *vqa_path, char *dir, size_t n)
{
	const char *slash = strrchr(vqa_path, '/');
	if (!slash) {
		snprintf(dir, n, ".");
		return;
	}
	{
		size_t len = (size_t)(slash - vqa_path);
		if (len >= n)
			len = n - 1;
		memcpy(dir, vqa_path, len);
		dir[len] = '\0';
	}
}

static int file_exists(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* Parse "stem.N.w16" → N, or -1 if not a match. */
static int parse_w16_seg(const char *name, const char *stem)
{
	size_t slen = strlen(stem);
	const char *p;
	char *end = NULL;
	unsigned long v;
	if (strncmp(name, stem, slen) != 0 || name[slen] != '.')
		return -1;
	p = name + slen + 1;
	if (*p < '0' || *p > '9')
		return -1;
	v = strtoul(p, &end, 10);
	if (!end || end == p || strcmp(end, ".w16") != 0 || v > 0x7ffffffful)
		return -1;
	return (int)v;
}

static int cmp_int(const void *a, const void *b)
{
	int ia = *(const int *)a;
	int ib = *(const int *)b;
	return (ia > ib) - (ia < ib);
}

static int collect_segments(const char *vqa_path, int *segs, int max_segs)
{
	char dir[512], stem[256];
	DIR *dp;
	struct dirent *de;
	int n = 0;

	dir_from_vqa(vqa_path, dir, sizeof(dir));
	stem_from_vqa(vqa_path, stem, sizeof(stem));
	dp = opendir(dir);
	if (!dp) {
		fprintf(stderr, "error: %s: %s\n", dir, strerror(errno));
		return -1;
	}
	while ((de = readdir(dp)) != NULL) {
		int seg = parse_w16_seg(de->d_name, stem);
		int i, dup = 0;
		if (seg < 0)
			continue;
		for (i = 0; i < n; i++) {
			if (segs[i] == seg) {
				dup = 1;
				break;
			}
		}
		if (dup)
			continue;
		if (n >= max_segs) {
			fprintf(stderr, "error: too many W16 sidecars (>%d)\n", max_segs);
			closedir(dp);
			return -1;
		}
		segs[n++] = seg;
	}
	closedir(dp);
	qsort(segs, (size_t)n, sizeof(int), cmp_int);
	return n;
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

static int run_refine_one(const StvqRefineW16Opts *opts, const char *pal, const char *hist, const char *w16,
    char **extra, int n_extra)
{
	char *argv[16 + MAX_EXTRA_ARGS];
	int argc = 0;
	int i;
	pid_t pid;
	int status;

	argv[argc++] = (char *)opts->palette_opt;
	argv[argc++] = "-c";
	argv[argc++] = "-p";
	argv[argc++] = (char *)pal;
	argv[argc++] = "-o";
	argv[argc++] = (char *)w16;
	argv[argc++] = "--hist";
	argv[argc++] = (char *)hist;
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

int stvq_refine_w16(const StvqRefineW16Opts *opts)
{
	int segs[MAX_SEGMENTS];
	int nsegs, s, n_extra = 0;
	char *extra_buf = NULL;
	char *extra[MAX_EXTRA_ARGS];
	int rc = -1;

	if (!opts || !opts->vqa_path || !opts->palette_opt || !opts->palette_opt[0]) {
		fprintf(stderr, "error: refine-w16 requires VQA path and --palette-opt\n");
		return -1;
	}

	if (opts->palette_opt_args && opts->palette_opt_args[0]) {
		extra_buf = strdup(opts->palette_opt_args);
		if (!extra_buf)
			return -1;
		n_extra = split_comma_args(extra_buf, extra, MAX_EXTRA_ARGS);
		if (n_extra >= MAX_EXTRA_ARGS && strchr(opts->palette_opt_args, ',')) {
			/* hit cap with more commas possible — warn */
			fprintf(stderr, "warning: --palette-opt-args truncated at %d tokens\n", MAX_EXTRA_ARGS);
		}
	}

	nsegs = collect_segments(opts->vqa_path, segs, MAX_SEGMENTS);
	if (nsegs < 0)
		goto done;
	if (nsegs == 0) {
		char stem[256];
		stem_from_vqa(opts->vqa_path, stem, sizeof(stem));
		fprintf(stderr, "error: no %s.<N>.w16 sidecars found next to %s\n", stem, opts->vqa_path);
		goto done;
	}

	fprintf(stderr, "refine-w16: %d segment(s), mode=%s\n", nsegs,
	    opts->mode == STVQ_REFINE_QUICK     ? "quick"
	        : opts->mode == STVQ_REFINE_THOROUGH ? "thorough"
	                                             : "normal");

	for (s = 0; s < nsegs; s++) {
		char pal[768], hist[768], w16[768];
		stvq_sidecar_paths(opts->vqa_path, segs[s], pal, hist, w16, sizeof(pal));
		if (!file_exists(w16)) {
			fprintf(stderr, "error: missing %s\n", w16);
			goto done;
		}
		if (!file_exists(pal)) {
			fprintf(stderr, "error: missing %s (required for refine)\n", pal);
			goto done;
		}
		if (!file_exists(hist)) {
			fprintf(stderr, "error: missing %s (required for refine)\n", hist);
			goto done;
		}
		fprintf(stderr, "  refining %s...\n", w16);
		if (run_refine_one(opts, pal, hist, w16, extra, n_extra) != 0)
			goto done;
	}

	rc = 0;
done:
	free(extra_buf);
	return rc;
}
