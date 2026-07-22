/*
 * stvq_fix_w16.c - Invoke w16fix on each palette-segment .w16 (in place).
 */
#include "stvq_fix_w16.h"

#include "stvq_palette.h"
#include "vqa_decode.h"

#include <errno.h>
#include <stdio.h>
#include <string.h> /* memset */
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int file_exists(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int run_w16fix(const StvqFixW16Opts *opts, const char *w16)
{
	char *argv[5];
	int argc = 0;
	pid_t pid;
	int status;
	int i;

	argv[argc++] = (char *)opts->w16fix;
	argv[argc++] = (char *)w16;
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
		execv(opts->w16fix, argv);
		fprintf(stderr, "error: execv %s: %s\n", opts->w16fix, strerror(errno));
		_exit(127);
	}
	if (waitpid(pid, &status, 0) < 0)
		return -1;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		fprintf(stderr, "error: w16fix failed for %s (status %d)\n", w16,
		    WIFEXITED(status) ? WEXITSTATUS(status) : -1);
		return -1;
	}
	return 0;
}

int stvq_fix_w16(const StvqFixW16Opts *opts)
{
	VqaDecode dec;
	unsigned s;
	int rc = -1;

	memset(&dec, 0, sizeof(dec));
	if (!opts || !opts->vqa_path || !opts->w16fix || !opts->w16fix[0]) {
		fprintf(stderr, "error: fix-w16 requires VQA path and --w16fix\n");
		return -1;
	}

	fprintf(stderr, "decoding %s...\n", opts->vqa_path);
	if (vqa_decode_file(opts->vqa_path, &dec) != 0) {
		fprintf(stderr, "error: VQA decode failed\n");
		return -1;
	}

	fprintf(stderr, "fix-w16: %u segment(s)%s\n", dec.segment_count, opts->dry_run ? " (dry-run)" : "");
	for (s = 0; s < dec.segment_count; s++) {
		char pal[768], hist[768], w16[768];
		stvq_sidecar_paths(opts->vqa_path, (int)s, pal, hist, w16, sizeof(pal));
		(void)pal;
		(void)hist;
		if (!file_exists(w16)) {
			if (opts->dry_run) {
				fprintf(stderr, "  fixing %s... (missing)\n", w16);
				if (run_w16fix(opts, w16) != 0)
					goto done;
				continue;
			}
			fprintf(stderr, "error: missing %s (run init-w16 / refine-w16 first)\n", w16);
			goto done;
		}
		fprintf(stderr, "  fixing %s...\n", w16);
		if (run_w16fix(opts, w16) != 0)
			goto done;
	}

	rc = 0;
done:
	vqa_decode_free(&dec);
	return rc;
}
