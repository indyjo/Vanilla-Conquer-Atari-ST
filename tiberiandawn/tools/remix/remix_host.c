/*
 * remix - Host utility to repack C&C MIX archives for the Atari ST port.
 */

#include "remix.h"
#include "remix_print.h"
#include "remix_shpx.h"
#include "remix_st16.h"

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

#define REMIX_MAX_INPUTS 8

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
	    "  %s -o output.mix first.mix second.mix [...]\n"
	    "  %s -d input_dir [-o output_dir]\n"
	    "\n"
	    "Repack C&C MIX archive(s):\n"
	    "  - autodetect file types\n"
	    "  - convert audio to 11025 Hz 8-bit mono PCM .AUD\n"
	    "  - convert theater terrain iconsets to ST16 (needs *.W16 in cwd)\n"
	    "  - convert KeyFrame SHPs to SHPX + poolnnnn.bin sidecar (--shpx)\n"
	    "  - convert audio to AUDX + pool sidecar (--audx; SOUNDS/SPEECH/SCORES)\n"
	    "  - convert VQA movies to STVQ (--convert-vqa; needs video/*.w16)\n"
	    "  - pad payloads to even byte offsets from MIX start\n"
	    "\n"
	    "Multiple inputs: merge by CRC+size (keep first), then repack.\n"
	    "\n"
	    "Options:\n"
	    "  -o, --output PATH       output MIX file, or output directory with -d\n"
	    "  -d, --directory DIR     remix all .mix/.MIX files in DIR (non-recursive)\n"
	    "  --w16-dir PATH          directory containing TEMPERAT.W16 and video/ (default: cwd)\n"
	    "  --no-st16-iconsets      skip ST16 iconset conversion\n"
	    "  --shpx                  convert KeyFrame SHPs to SHPX (CONQUER/TEMPERAT/DESERT/WINTER)\n"
	    "  --shpx-verbose          per-shape SHPX/clip details on stderr (requires --shpx)\n"
	    "  --pool-id ID            SHPX pool id (default from MIX name; requires --shpx)\n"
	    "  --audx                  convert PCM AUD to AUDX (SOUNDS/SPEECH/SCORES)\n"
	    "  --audx-pool-id ID       AUDX pool id (default from MIX name; requires --audx)\n"
	    "  --convert-vqa           convert VQA payloads to STVQ (needs video/xxxxxxxx.N.w16)\n"
	    "  --video-quality Q       low|medium|high (default medium; low suits 8 MHz ST; requires --convert-vqa)\n"
	    "  --video-effort E        fast|normal|thorough (default normal; requires --convert-vqa)\n"
	    "  -h, --help              show this help\n",
	    prog, prog, prog);
}

static int remix_directory(const char *dir, const char *out_dir, RemixConfig *cfg)
{
	DIR *dp;
	struct dirent *ent;
	char in_path[PATH_MAX];
	char out_path[PATH_MAX];
	RemixStats stats;
	unsigned count = 0;
	unsigned ok = 0;

	remix_stats_init(&stats);

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
		cfg->mix_basename = ent->d_name;

		if (out_dir) {
			if (!path_join(out_path, sizeof(out_path), out_dir, ent->d_name))
				continue;
			rc = remix_mix_file(in_path, out_path, cfg, &stats);
		} else {
			char tmp[PATH_MAX];
			if (!make_temp_path(tmp, sizeof(tmp), in_path)) {
				fprintf(stderr, "error: cannot create temporary file for %s\n", in_path);
				closedir(dp);
				return 0;
			}
			rc = remix_mix_file(in_path, tmp, cfg, &stats);
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
	const char *inputs[REMIX_MAX_INPUTS];
	unsigned input_count = 0;
	const char *out_path = NULL;
	const char *in_dir = NULL;
	const char *w16_dir = NULL;
	RemixConfig cfg;
	RemixStats stats;
	char mix_base[256];
	int argi;
	int rc;
	unsigned i;
	int pool_id_set = 0;
	int audx_pool_id_set = 0;

	remix_stats_init(&stats);
	memset(&cfg, 0, sizeof(cfg));
	cfg.ui = REMIX_UI_HOST;
	cfg.fallback_copy_on_convert_fail = 1;
	cfg.convert_st16_iconsets = 1;
	cfg.video_quality = REMIX_VIDEO_QUALITY_MEDIUM;
	cfg.video_effort = REMIX_VIDEO_EFFORT_NORMAL;
	/* shpx_pool_id / audx_pool_id 0 → remix_mix_file_ex picks default from mix basename */

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
		} else if (!strcmp(argv[argi], "--w16-dir")) {
			if (argi + 1 >= argc) {
				fprintf(stderr, "error: %s requires a path\n", argv[argi]);
				return 1;
			}
			w16_dir = argv[++argi];
		} else if (!strcmp(argv[argi], "--no-st16-iconsets")) {
			cfg.convert_st16_iconsets = 0;
		} else if (!strcmp(argv[argi], "--shpx")) {
			cfg.convert_shpx = 1;
		} else if (!strcmp(argv[argi], "--shpx-verbose")) {
			cfg.shpx_verbose = 1;
		} else if (!strcmp(argv[argi], "--audx")) {
			cfg.convert_audx = 1;
		} else if (!strcmp(argv[argi], "--convert-vqa")) {
			cfg.convert_vqa = 1;
		} else if (!strcmp(argv[argi], "--video-quality")) {
			const char *q;
			if (argi + 1 >= argc) {
				fprintf(stderr, "error: %s requires low|medium|high\n", argv[argi]);
				return 1;
			}
			q = argv[++argi];
			if (!strcmp(q, "low"))
				cfg.video_quality = REMIX_VIDEO_QUALITY_LOW;
			else if (!strcmp(q, "medium"))
				cfg.video_quality = REMIX_VIDEO_QUALITY_MEDIUM;
			else if (!strcmp(q, "high"))
				cfg.video_quality = REMIX_VIDEO_QUALITY_HIGH;
			else {
				fprintf(stderr, "error: --video-quality must be low|medium|high\n");
				return 1;
			}
		} else if (!strcmp(argv[argi], "--video-effort")) {
			const char *e;
			if (argi + 1 >= argc) {
				fprintf(stderr, "error: %s requires fast|normal|thorough\n", argv[argi]);
				return 1;
			}
			e = argv[++argi];
			if (!strcmp(e, "fast"))
				cfg.video_effort = REMIX_VIDEO_EFFORT_FAST;
			else if (!strcmp(e, "normal"))
				cfg.video_effort = REMIX_VIDEO_EFFORT_NORMAL;
			else if (!strcmp(e, "thorough"))
				cfg.video_effort = REMIX_VIDEO_EFFORT_THOROUGH;
			else {
				fprintf(stderr, "error: --video-effort must be fast|normal|thorough\n");
				return 1;
			}
		} else if (!strcmp(argv[argi], "--pool-id")) {
			unsigned long id;
			char *end;

			if (argi + 1 >= argc) {
				fprintf(stderr, "error: %s requires a value\n", argv[argi]);
				return 1;
			}
			id = strtoul(argv[++argi], &end, 0);
			if (!argv[argi][0] || (end && *end != '\0') || id == 0 || id > 0xFFFFu) {
				fprintf(stderr, "error: %s must be 1..65535\n", "--pool-id");
				return 1;
			}
			cfg.shpx_pool_id = (uint16_t)id;
			pool_id_set = 1;
		} else if (!strcmp(argv[argi], "--audx-pool-id")) {
			unsigned long id;
			char *end;

			if (argi + 1 >= argc) {
				fprintf(stderr, "error: %s requires a value\n", argv[argi]);
				return 1;
			}
			id = strtoul(argv[++argi], &end, 0);
			if (!argv[argi][0] || (end && *end != '\0') || id == 0 || id > 0xFFFFu) {
				fprintf(stderr, "error: %s must be 1..65535\n", "--audx-pool-id");
				return 1;
			}
			cfg.audx_pool_id = (uint16_t)id;
			audx_pool_id_set = 1;
		} else if (!strcmp(argv[argi], "-h") || !strcmp(argv[argi], "--help")) {
			usage(argv[0]);
			return 0;
		} else if (argv[argi][0] == '-') {
			fprintf(stderr, "error: unknown option %s\n", argv[argi]);
			usage(argv[0]);
			return 1;
		} else {
			if (input_count >= REMIX_MAX_INPUTS) {
				fprintf(stderr, "error: too many input files (max %d)\n", REMIX_MAX_INPUTS);
				return 1;
			}
			inputs[input_count++] = argv[argi];
		}
	}

	if (in_dir && input_count > 0) {
		fprintf(stderr, "error: use either -d or input MIX file(s), not both\n");
		return 1;
	}

	if (pool_id_set && !cfg.convert_shpx) {
		fprintf(stderr, "error: --pool-id requires --shpx\n");
		return 1;
	}

	if (audx_pool_id_set && !cfg.convert_audx) {
		fprintf(stderr, "error: --audx-pool-id requires --audx\n");
		return 1;
	}

	if (cfg.shpx_verbose && !cfg.convert_shpx) {
		fprintf(stderr, "error: --shpx-verbose requires --shpx\n");
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
		cfg.w16_dir = w16_dir;
		return remix_directory(in_dir, out_path, &cfg) ? 0 : 1;
	}

	if (input_count == 0 || !out_path) {
		usage(argv[0]);
		return 1;
	}

	for (i = 0; i < input_count; ++i) {
		if (paths_same(inputs[i], out_path)) {
			fprintf(stderr, "error: output path must differ from input path %s\n", inputs[i]);
			return 1;
		}
	}

	cfg.w16_dir = w16_dir;
	if (input_count == 1)
		remix_path_basename(inputs[0], mix_base, sizeof(mix_base));
	else
		remix_path_basename(out_path, mix_base, sizeof(mix_base));
	cfg.mix_basename = mix_base;

	rc = remix_mix_merge_and_repack(out_path, inputs, input_count, &cfg, &stats);
	if (rc < 0) {
		fprintf(stderr, "error: unsupported MIX input\n");
		return 1;
	}
	return rc > 0 ? 0 : 1;
}
