#include "remix_print.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void trim_line(char *buf, size_t cap)
{
	size_t len = strlen(buf);
	if (len >= cap)
		buf[cap - 1] = '\0';
}

void remix_print_st_banner(void)
{
	printf("remix - Atari ST MIX repack\n");
}

void remix_print_st_mix_header(const char *in_path, unsigned count)
{
	char line[REMIX_LINE_WIDTH + 1];

	(void)count;
	snprintf(line, sizeof(line), "=== %s ===", in_path);
	trim_line(line, sizeof(line));
	printf("%s\n", line);
	printf("CRC         SIZE TYPE\n");
}

void remix_print_st_mix_done(const char *path, unsigned count, uint32_t data_size)
{
	char line[REMIX_LINE_WIDTH + 1];

	(void)data_size;
	snprintf(line, sizeof(line), "wrote %s (%u files)", path, count);
	trim_line(line, sizeof(line));
	printf("%s\n", line);
}

void remix_print_st_entry_line(uint32_t crc, uint32_t size, const char *type_in, const char *type_out)
{
	char line[REMIX_LINE_WIDTH + 1];

	if (type_out && type_out[0] && strcmp(type_in, type_out) != 0)
		snprintf(line, sizeof(line), "%08X %7u %s", (unsigned)crc, (unsigned)size, type_out);
	else
		snprintf(line, sizeof(line), "%08X %7u %s", (unsigned)crc, (unsigned)size, type_in);
	trim_line(line, sizeof(line));
	printf("%s\n", line);
}

static int g_st_progress_active;
static int g_st_progress_prefix;
static unsigned g_st_progress_dots;

void remix_print_st_progress_erase(void)
{
	unsigned i;

	if (!g_st_progress_prefix && !g_st_progress_active)
		return;
	fputc('\r', stdout);
	for (i = 0; i < REMIX_LINE_WIDTH; ++i)
		fputc(' ', stdout);
	fputc('\r', stdout);
	fflush(stdout);
	g_st_progress_active = 0;
	g_st_progress_prefix = 0;
	g_st_progress_dots = 0;
}

void remix_print_st_progress_reset(void)
{
	remix_print_st_progress_erase();
}

void remix_print_st_progress(const char *verb, unsigned done, unsigned total)
{
	unsigned dots;
	unsigned i;

	if (!g_st_progress_prefix) {
		fprintf(stdout, "%*s ", REMIX_VERB_WIDTH, verb);
		fflush(stdout);
		g_st_progress_prefix = 1;
		g_st_progress_active = 1;
	}

	if (total == 0)
		total = 1;
	dots = (unsigned)((uint64_t)done * REMIX_PROGRESS_DOTS / total);
	if (done >= total)
		dots = REMIX_PROGRESS_DOTS;
	else if (dots > REMIX_PROGRESS_DOTS)
		dots = REMIX_PROGRESS_DOTS;

	if (dots <= g_st_progress_dots && done < total)
		return;

	for (i = g_st_progress_dots; i < dots; ++i)
		fputc('.', stdout);
	g_st_progress_dots = dots;
	fflush(stdout);

	if (done >= total) {
		for (i = g_st_progress_dots; i < REMIX_PROGRESS_DOTS; ++i)
			fputc('.', stdout);
		fflush(stdout);
		remix_print_st_progress_erase();
	}
}

void remix_print_st_await_keypress(void)
{
	printf("\nPress any key to continue...");
	fflush(stdout);
	(void)getchar();
}

void remix_print_st_warn(const char *msg)
{
	char line[REMIX_LINE_WIDTH + 1];

	snprintf(line, sizeof(line), "%s", msg);
	trim_line(line, sizeof(line));
	printf("%s\n", line);
}

static unsigned st_summary_digits(unsigned n)
{
	unsigned d = 1;

	if (n == 0)
		return 1;
	while (n >= 10) {
		n /= 10;
		++d;
	}
	return d;
}

static unsigned st_summary_num_width(const RemixStats *stats)
{
	unsigned w = 1;
	unsigned mix_err = stats->mix_files_error + stats->mix_files_skipped;

	w = st_summary_digits(stats->mix_files_ok);
	if (st_summary_digits(mix_err) > w)
		w = st_summary_digits(mix_err);
	if (st_summary_digits(stats->payload_files) > w)
		w = st_summary_digits(stats->payload_files);
	if (st_summary_digits(stats->payload_errors) > w)
		w = st_summary_digits(stats->payload_errors);
	if (st_summary_digits(stats->audio_files) > w)
		w = st_summary_digits(stats->audio_files);
	if (st_summary_digits(stats->audio_converted) > w)
		w = st_summary_digits(stats->audio_converted);
	if (st_summary_digits(stats->audio_already_ok) > w)
		w = st_summary_digits(stats->audio_already_ok);
	return w;
}

static void print_st_summary_row(const char *indent, const char *label, unsigned value, unsigned num_w)
{
	char line[REMIX_LINE_WIDTH + 1];
	int const indent_len = (int)strlen(indent);
	int const label_len = (int)strlen(label);
	int const pad = REMIX_LINE_WIDTH - indent_len - label_len - (int)num_w;

	if (pad < 1) {
		snprintf(line, sizeof(line), "%s%s%*u", indent, label, (int)num_w, value);
	} else {
		snprintf(line, sizeof(line), "%s%s%*s%*u", indent, label, pad, "", (int)num_w, value);
	}
	trim_line(line, sizeof(line));
	printf("%s\n", line);
}

void remix_print_st_summary(const RemixStats *stats)
{
	unsigned num_w;
	unsigned mix_err;

	if (!stats)
		return;

	num_w = st_summary_num_width(stats);
	mix_err = stats->mix_files_error + stats->mix_files_skipped;

	print_st_summary_row("", "Mix files:", stats->mix_files_ok, num_w);
	print_st_summary_row("  ", "Errors:", mix_err, num_w);
	print_st_summary_row("", "Payload files:", stats->payload_files, num_w);
	print_st_summary_row("  ", "Errors:", stats->payload_errors, num_w);
	print_st_summary_row("  ", "Audio files:", stats->audio_files, num_w);
	print_st_summary_row("    ", "Converted:", stats->audio_converted, num_w);
	if (stats->audio_already_ok > 0)
		print_st_summary_row("    ", "Unchanged:", stats->audio_already_ok, num_w);
	if (stats->audx_files > 0 || stats->audx_converted > 0 || stats->audx_errors > 0) {
		print_st_summary_row("  ", "AUDX files:", stats->audx_files, num_w);
		print_st_summary_row("    ", "Converted:", stats->audx_converted, num_w);
		if (stats->audx_errors > 0)
			print_st_summary_row("    ", "Errors:", stats->audx_errors, num_w);
	}
}

void remix_print_host_banner(const char *in_path, const char *out_path, unsigned count, uint32_t data_start)
{
	printf("%s -> %s (%u files, data at %u):\n", in_path, out_path, count, data_start);
}

void remix_print_host_table_header(void)
{
	printf("%-*s  %-*s  %-*s  %-*s  %-*s  type\n",
	    10, "CRC", 11, "old_off", 11, "new_off", 11, "old_size", 11, "new_size");
}

void remix_print_host_entry(const RemixEntry *e)
{
	printf("0x%08X  %11u  %11u  %11u  %11u  %s",
	    (unsigned)e->crc,
	    (unsigned)e->old_offset,
	    (unsigned)e->new_offset,
	    (unsigned)e->old_size,
	    (unsigned)e->new_size,
	    e->type_in);
	if (strcmp(e->type_in, e->type_out) != 0)
		printf(" -> %s", e->type_out);
	printf("\n");
}

void remix_print_host_mix_done(const char *path, unsigned count, uint32_t data_size)
{
	printf("wrote %s (%u files, %u data bytes)\n", path, count, data_size);
}
