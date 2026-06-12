/*
 * remix - Host utility to repack C&C MIX archives for the Atari ST port.
 */

#include "remix.h"
#include "remix_print.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static int paths_same(const char *a, const char *b)
{
	if (strcmp(a, b) == 0)
		return 1;
#if defined(_POSIX_VERSION) || defined(__unix__) || defined(__APPLE__)
	{
		char *ra = realpath(a, NULL);
		char *rb = realpath(b, NULL);
		int same = 0;

		if (ra && rb)
			same = (strcmp(ra, rb) == 0);
		free(ra);
		free(rb);
		return same;
	}
#else
	return 0;
#endif
}

static int path_is_directory(const char *path)
{
	struct stat st;

	if (stat(path, &st) != 0)
		return 0;
	return S_ISDIR(st.st_mode);
}

static int has_mix_extension(const char *name)
{
	size_t len = strlen(name);

	if (len < 5)
		return 0;
	return strcasecmp(name + len - 4, ".mix") == 0;
}

static int path_join(char *dst, size_t dst_cap, const char *dir, const char *name)
{
	size_t dir_len = strlen(dir);
	int need_sep = (dir_len > 0 && dir[dir_len - 1] != '/');

	if (need_sep) {
		if (snprintf(dst, dst_cap, "%s/%s", dir, name) >= (int)dst_cap)
			return 0;
	} else {
		if (snprintf(dst, dst_cap, "%s%s", dir, name) >= (int)dst_cap)
			return 0;
	}
	return 1;
}

static int make_temp_path(char *dst, size_t cap, const char *final_path)
{
	size_t len = strlen(final_path);
	int fd;

	if (len + strlen(".remix.XXXXXX") + 1 > cap)
		return 0;
	if (snprintf(dst, cap, "%s.remix.XXXXXX", final_path) >= (int)cap)
		return 0;

	fd = mkstemp(dst);
	if (fd < 0)
		return 0;
	if (close(fd) != 0) {
		unlink(dst);
		return 0;
	}
	return 1;
}

static void usage(const char *prog)
{
	fprintf(stderr,
	    "Usage:\n"
	    "  %s -o output.mix input.mix\n"
	    "  %s -d input_dir [-o output_dir]\n"
	    "\n"
	    "Repack C&C MIX archive(s):\n"
	    "  - autodetect embedded file types\n"
	    "  - convert audio to 11025 Hz 8-bit mono PCM .AUD\n"
	    "  - pad payloads so each begins at an even offset from the MIX start\n"
	    "\n"
	    "Options:\n"
	    "  -o, --output PATH   output MIX file, or output directory with -d\n"
	    "  -d, --directory DIR remix all .mix/.MIX files in DIR (non-recursive)\n"
	    "  -h, --help          show this help\n",
	    prog, prog);
}

static int remix_directory(const char *dir, const char *out_dir)
{
	DIR *dp;
	struct dirent *ent;
	char in_path[PATH_MAX];
	char out_path[PATH_MAX];
	RemixConfig cfg;
	RemixStats stats;
	unsigned count = 0;
	unsigned ok = 0;

	remix_stats_init(&stats);
	memset(&cfg, 0, sizeof(cfg));
	cfg.ui = REMIX_UI_HOST;

	dp = opendir(dir);
	if (!dp) {
		fprintf(stderr, "error: cannot open directory %s\n", dir);
		return 0;
	}

	while ((ent = readdir(dp)) != NULL) {
		int rc;

		if (!has_mix_extension(ent->d_name))
			continue;
		if (!path_join(in_path, sizeof(in_path), dir, ent->d_name))
			continue;

		++count;
		printf("=== %s ===\n", ent->d_name);

		if (out_dir) {
			if (!path_join(out_path, sizeof(out_path), out_dir, ent->d_name))
				continue;
			rc = remix_mix_file(in_path, out_path, &cfg, &stats);
		} else {
			char tmp[PATH_MAX];
			if (!make_temp_path(tmp, sizeof(tmp), in_path)) {
				fprintf(stderr, "error: cannot create temporary file for %s\n", in_path);
				closedir(dp);
				return 0;
			}
			rc = remix_mix_file(in_path, tmp, &cfg, &stats);
			if (rc > 0 && rename(tmp, in_path) != 0) {
				fprintf(stderr, "error: cannot replace %s\n", in_path);
				unlink(tmp);
				rc = 0;
			} else if (rc <= 0) {
				unlink(tmp);
			}
		}

		if (rc > 0)
			++ok;
		else if (rc < 0) {
			fprintf(stderr, "error: unsupported MIX %s\n", in_path);
			++stats.mix_files_skipped;
		} else {
			fprintf(stderr, "error: failed to remix %s\n", in_path);
			++stats.mix_files_error;
			closedir(dp);
			return 0;
		}
		printf("\n");
	}
	closedir(dp);

	if (count == 0) {
		fprintf(stderr, "error: no .mix files found in %s\n", dir);
		return 0;
	}

	if (out_dir)
		printf("remixed %u/%u MIX file(s) from %s to %s\n", ok, count, dir, out_dir);
	else
		printf("remixed %u/%u MIX file(s) in %s\n", ok, count, dir);
	return 1;
}

int main(int argc, char **argv)
{
	const char *in_path = NULL;
	const char *out_path = NULL;
	const char *in_dir = NULL;
	RemixConfig cfg;
	RemixStats stats;
	int argi;
	int rc;

	remix_stats_init(&stats);
	memset(&cfg, 0, sizeof(cfg));
	cfg.ui = REMIX_UI_HOST;

	for (argi = 1; argi < argc; ++argi) {
		if (!strcmp(argv[argi], "-o") || !strcmp(argv[argi], "--output")) {
			if (argi + 1 >= argc) {
				fprintf(stderr, "error: %s requires a path\n", argv[argi]);
				return 1;
			}
			out_path = argv[++argi];
		} else if (!strcmp(argv[argi], "-d") || !strcmp(argv[argi], "--directory")) {
			if (argi + 1 >= argc) {
				fprintf(stderr, "error: %s requires a path\n", argv[argi]);
				return 1;
			}
			in_dir = argv[++argi];
		} else if (!strcmp(argv[argi], "-h") || !strcmp(argv[argi], "--help")) {
			usage(argv[0]);
			return 0;
		} else if (argv[argi][0] == '-') {
			fprintf(stderr, "error: unknown option %s\n", argv[argi]);
			usage(argv[0]);
			return 1;
		} else if (in_path) {
			fprintf(stderr, "error: unexpected argument %s\n", argv[argi]);
			usage(argv[0]);
			return 1;
		} else {
			in_path = argv[argi];
		}
	}

	if (in_dir && in_path) {
		fprintf(stderr, "error: use either -d or a single input MIX file, not both\n");
		return 1;
	}

	if (in_dir) {
		if (!path_is_directory(in_dir)) {
			fprintf(stderr, "error: %s is not a directory\n", in_dir);
			return 1;
		}
		if (out_path && !path_is_directory(out_path)) {
			fprintf(stderr, "error: %s is not a directory (expected output directory with -d)\n", out_path);
			return 1;
		}
		return remix_directory(in_dir, out_path) ? 0 : 1;
	}

	if (!in_path || !out_path) {
		usage(argv[0]);
		return 1;
	}
	if (paths_same(in_path, out_path)) {
		fprintf(stderr, "error: output path must differ from input path\n");
		return 1;
	}

	rc = remix_mix_file(in_path, out_path, &cfg, &stats);
	if (rc < 0) {
		fprintf(stderr, "error: unsupported MIX %s\n", in_path);
		return 1;
	}
	return rc > 0 ? 0 : 1;
}
