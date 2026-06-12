/*
 * remix.tos - MiNT utility to repack C&C MIX archives in the current directory.
 */

#include "remix.h"
#include "remix_print.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static int has_mix_extension(const char *name)
{
	size_t len = strlen(name);

	if (len < 5)
		return 0;
	return strcasecmp(name + len - 4, ".mix") == 0;
}

static int prompt_temp_mxx(void)
{
	char line[16];

	printf("temp.mxx already exists.\n");
	printf("[A]bort  [D]elete and continue? ");
	fflush(stdout);
	if (!fgets(line, sizeof(line), stdin))
		return 0;
	if (line[0] == 'D' || line[0] == 'd') {
		if (remove(REMIX_TEMP_MXX) != 0) {
			perror("remove temp.mxx");
			return 0;
		}
		return 1;
	}
	return 0;
}

static int remix_cwd(void)
{
	DIR *dp;
	struct dirent *ent;
	RemixConfig cfg;
	RemixStats stats;
	char cwd[512];

	remix_stats_init(&stats);
	memset(&cfg, 0, sizeof(cfg));
	cfg.ui = REMIX_UI_ST;
	cfg.fallback_copy_on_convert_fail = 1;

	if (access(REMIX_TEMP_MXX, F_OK) == 0 && !prompt_temp_mxx())
		return 1;

	remix_print_st_banner();
	if (getcwd(cwd, sizeof(cwd)))
		printf("Working directory: %s\n", cwd);

	dp = opendir(".");
	if (!dp) {
		perror("opendir");
		return 1;
	}

	while ((ent = readdir(dp)) != NULL) {
		int rc;

		if (!has_mix_extension(ent->d_name))
			continue;

		printf("\n");
		rc = remix_mix_file_inplace(ent->d_name, REMIX_TEMP_MXX, &cfg, &stats);
		if (rc > 0)
			continue;
		if (rc < 0) {
			char warn[REMIX_LINE_WIDTH + 1];
			snprintf(warn, sizeof(warn), "SKIP %s unsupported header", ent->d_name);
			remix_print_st_warn(warn);
			++stats.mix_files_skipped;
			remove(REMIX_TEMP_MXX);
			continue;
		}

		fprintf(stderr, "error: failed to remix %s\n", ent->d_name);
		++stats.mix_files_error;
		remove(REMIX_TEMP_MXX);
	}

	closedir(dp);
	printf("\n");
	remix_print_st_summary(&stats);
	remix_print_st_await_keypress();
	return (stats.mix_files_error == 0) ? 0 : 1;
}

int main(void)
{
	return remix_cwd();
}
