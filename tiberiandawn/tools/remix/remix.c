/*
 * remix - Repack a C&C MIX archive with even-aligned payloads and optional
 * asset conversions for the Atari ST port.
 *
 * Processes one embedded file at a time (O(total bytes), bounded per-file RAM),
 * autodetects type, prints a status line per entry, and writes a new MIX with
 * 2-byte-aligned payload offsets relative to the start of the archive.
 */

#include "remix_aud.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

enum { REMIX_COPY_CHUNK = 65536 };

/* Status table column layout (shared by header and rows). */
enum {
	REMIX_COL_CRC = 10,
	REMIX_COL_NUM = 11
};

typedef struct {
	uint32_t crc;
	uint32_t old_offset;
	uint32_t old_size;
	uint32_t new_offset;
	uint32_t new_size;
	char type_in[20];
	char type_out[20];
} RemixEntry;

typedef struct {
	uint16_t count;
	uint32_t data_size;
	uint32_t data_start;
	RemixEntry *entries;
} RemixMix;

static int remix_file(const char *in_path, const char *out_path);

static uint16_t read_le16(const unsigned char *p)
{
	return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_le16(unsigned char *p, uint16_t v)
{
	p[0] = (unsigned char)(v & 0xFFu);
	p[1] = (unsigned char)((v >> 8) & 0xFFu);
}

static void write_le32(unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char)(v & 0xFFu);
	p[1] = (unsigned char)((v >> 8) & 0xFFu);
	p[2] = (unsigned char)((v >> 16) & 0xFFu);
	p[3] = (unsigned char)((v >> 24) & 0xFFu);
}

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

static int remix_file_inplace(const char *path)
{
	char tmp[PATH_MAX];

	if (!make_temp_path(tmp, sizeof(tmp), path)) {
		fprintf(stderr, "error: cannot create temporary file for %s\n", path);
		return 0;
	}

	if (!remix_file(path, tmp)) {
		unlink(tmp);
		return 0;
	}

	if (rename(tmp, path) != 0) {
		fprintf(stderr, "error: cannot replace %s\n", path);
		unlink(tmp);
		return 0;
	}
	return 1;
}

static int remix_directory(const char *dir, const char *out_dir)
{
	DIR *dp;
	struct dirent *ent;
	char in_path[PATH_MAX];
	char out_path[PATH_MAX];
	unsigned count = 0;
	unsigned ok = 0;

	dp = opendir(dir);
	if (!dp) {
		fprintf(stderr, "error: cannot open directory %s\n", dir);
		return 0;
	}

	while ((ent = readdir(dp)) != NULL) {
		int success;

		if (!has_mix_extension(ent->d_name))
			continue;

		if (!path_join(in_path, sizeof(in_path), dir, ent->d_name))
			continue;

		++count;
		printf("=== %s ===\n", ent->d_name);

		if (out_dir) {
			if (!path_join(out_path, sizeof(out_path), out_dir, ent->d_name))
				continue;
			if (paths_same(in_path, out_path)) {
				fprintf(stderr, "error: output path equals input path for %s\n", ent->d_name);
				closedir(dp);
				return 0;
			}
			success = remix_file(in_path, out_path);
		} else {
			success = remix_file_inplace(in_path);
		}

		if (success)
			++ok;
		else {
			fprintf(stderr, "error: failed to remix %s\n", in_path);
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

static int looks_like_aud(
    const unsigned char *data, size_t probe_len, uint32_t file_size, char *type_out, size_t type_out_len)
{
	uint32_t comp_size;
	uint32_t uncomp;
	unsigned char flags;
	unsigned char compression;
	uint32_t payload_avail;

	if (probe_len < (size_t)REMIX_AUD_HDR_LEN)
		return 0;

	flags = data[10];
	compression = data[11];
	comp_size = read_le32(data + 2);
	uncomp = read_le32(data + 6);

	if (file_size > (uint32_t)REMIX_AUD_HDR_LEN)
		payload_avail = file_size - (uint32_t)REMIX_AUD_HDR_LEN;
	else
		payload_avail = 0;

	if (comp_size == 0)
		return 0;
	if (payload_avail != 0 && comp_size > payload_avail)
		return 0;
	if (comp_size > REMIX_AUD99_MAX_COMPRESSED_PAYLOAD)
		return 0;
	if (uncomp == 0 || uncomp > REMIX_AUD99_MAX_DECODED_PCM_BYTES)
		return 0;

	if (compression == REMIX_AUD_COMP_IMA99) {
		snprintf(type_out, type_out_len, "aud99");
		return 1;
	}
	if (compression == REMIX_AUD_COMP_PCM) {
		if ((flags & REMIX_AUD_FLAG_STEREO) != 0)
			snprintf(type_out, type_out_len, "aud_pcm_st");
		else if ((flags & REMIX_AUD_FLAG_16BIT) != 0)
			snprintf(type_out, type_out_len, "aud_pcm16");
		else if ((flags & REMIX_AUD_FLAG_DUP2X) != 0)
			snprintf(type_out, type_out_len, "aud_pcm11");
		else
			snprintf(type_out, type_out_len, "aud_pcm");
		return 1;
	}

	snprintf(type_out, type_out_len, "aud_%u", (unsigned)compression);
	return 1;
}

static int looks_like_voc(const unsigned char *data, size_t len, char *type_out, size_t type_out_len)
{
	if (len >= 19 && memcmp(data, "Creative Voice File", 19) == 0) {
		snprintf(type_out, type_out_len, "voc");
		return 1;
	}
	return 0;
}

static int looks_like_vqa(const unsigned char *data, size_t len, char *type_out, size_t type_out_len)
{
	if (len >= 4 && data[0] == 'F' && data[1] == 'O' && data[2] == 'R' && data[3] == 'M') {
		snprintf(type_out, type_out_len, "vqa");
		return 1;
	}
	return 0;
}

static int looks_like_pcx(const unsigned char *data, size_t len, char *type_out, size_t type_out_len)
{
	if (len >= 2 && data[0] == 0x0A && (data[1] == 0 || data[1] == 5)) {
		snprintf(type_out, type_out_len, "pcx");
		return 1;
	}
	return 0;
}

static int looks_like_pal(uint32_t file_size, char *type_out, size_t type_out_len)
{
	if (file_size == 768 || file_size == 768 + 4) {
		snprintf(type_out, type_out_len, "pal");
		return 1;
	}
	return 0;
}

static int looks_like_ini(const unsigned char *data, size_t len, char *type_out, size_t type_out_len)
{
	size_t i;
	size_t printable = 0;

	if (len == 0)
		return 0;
	for (i = 0; i < len && i < 256; ++i) {
		unsigned char c = data[i];
		if (c == 0)
			break;
		if (c == '\r' || c == '\n' || c == '\t' || (c >= 32 && c < 127))
			++printable;
	}
	if (printable >= 8 && (data[0] == '[' || (data[0] >= 'A' && data[0] <= 'Z'))) {
		snprintf(type_out, type_out_len, "ini");
		return 1;
	}
	return 0;
}

static int looks_like_map(uint32_t file_size, char *type_out, size_t type_out_len)
{
	/* TD scenario maps are exactly 8192 bytes. */
	if (file_size == 8192) {
		snprintf(type_out, type_out_len, "map");
		return 1;
	}
	return 0;
}

static int looks_like_shp(const unsigned char *data, size_t len, char *type_out, size_t type_out_len)
{
	uint16_t count;
	uint32_t first_off;

	if (len < 6)
		return 0;
	count = read_le16(data);
	if (count == 0 || count > 4096)
		return 0;
	if ((size_t)2 + (size_t)count * 4u + 4u > len)
		return 0;
	first_off = read_le32(data + 2);
	if (first_off < (uint32_t)(2 + count * 4) || first_off >= len)
		return 0;
	snprintf(type_out, type_out_len, "shp");
	return 1;
}

static void detect_file_type(
    const unsigned char *data, size_t probe_len, uint32_t file_size, char *type_out, size_t type_out_len)
{
	type_out[0] = '\0';
	if (looks_like_aud(data, probe_len, file_size, type_out, type_out_len))
		return;
	if (looks_like_voc(data, probe_len, type_out, type_out_len))
		return;
	if (looks_like_vqa(data, probe_len, type_out, type_out_len))
		return;
	if (looks_like_pcx(data, probe_len, type_out, type_out_len))
		return;
	if (looks_like_shp(data, probe_len, type_out, type_out_len))
		return;
	if (looks_like_pal(file_size, type_out, type_out_len))
		return;
	if (looks_like_ini(data, probe_len, type_out, type_out_len))
		return;
	if (looks_like_map(file_size, type_out, type_out_len))
		return;
	snprintf(type_out, type_out_len, "binary");
}

static int read_plain_mix_header(FILE *f, RemixMix *mix)
{
	unsigned char hdr[6];
	long pos;

	if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
		fprintf(stderr, "error: short read (MIX header)\n");
		return 0;
	}

	mix->count = read_le16(hdr);
	mix->data_size = read_le32(hdr + 2);
	if (mix->count == 0) {
		fprintf(stderr, "error: empty MIX or unsupported extended/encrypted header\n");
		return 0;
	}

	pos = ftell(f);
	if (pos < 0) {
		fprintf(stderr, "error: ftell failed\n");
		return 0;
	}
	mix->data_start = (uint32_t)pos + (uint32_t)mix->count * 12u;
	mix->entries = (RemixEntry *)calloc(mix->count, sizeof(RemixEntry));
	if (!mix->entries) {
		fprintf(stderr, "error: out of memory\n");
		return 0;
	}

	{
		unsigned i;
		for (i = 0; i < mix->count; ++i) {
			unsigned char sb[12];
			if (fread(sb, 1, sizeof(sb), f) != sizeof(sb)) {
				fprintf(stderr, "error: short read (MIX index entry %u)\n", i);
				return 0;
			}
			mix->entries[i].crc = read_le32(sb);
			mix->entries[i].old_offset = read_le32(sb + 4);
			mix->entries[i].old_size = read_le32(sb + 8);
		}
	}
	return 1;
}

static void free_mix(RemixMix *mix)
{
	free(mix->entries);
	mix->entries = NULL;
}

static int stream_copy(FILE *in, FILE *out, size_t nbytes)
{
	unsigned char chunk[REMIX_COPY_CHUNK];

	while (nbytes > 0) {
		size_t n = nbytes > REMIX_COPY_CHUNK ? REMIX_COPY_CHUNK : nbytes;
		if (fread(chunk, 1, n, in) != n)
			return 0;
		if (fwrite(chunk, 1, n, out) != n)
			return 0;
		nbytes -= n;
	}
	return 1;
}

static void print_table_header(void)
{
	printf("%-*s  %-*s  %-*s  %-*s  %-*s  type\n",
	    REMIX_COL_CRC, "CRC",
	    REMIX_COL_NUM, "old_off",
	    REMIX_COL_NUM, "new_off",
	    REMIX_COL_NUM, "old_size",
	    REMIX_COL_NUM, "new_size");
}

static void print_status(const RemixEntry *e)
{
	printf("0x%08" PRIX32 "  %*" PRIu32 "  %*" PRIu32 "  %*" PRIu32 "  %*" PRIu32 "  %s",
	    e->crc,
	    REMIX_COL_NUM, e->old_offset,
	    REMIX_COL_NUM, e->new_offset,
	    REMIX_COL_NUM, e->old_size,
	    REMIX_COL_NUM, e->new_size,
	    e->type_in);
	if (strcmp(e->type_in, e->type_out) != 0)
		printf(" -> %s", e->type_out);
	printf("\n");
}

static int write_placeholder_header(FILE *out, const RemixMix *mix)
{
	unsigned char hdr[6];
	unsigned char sb[12];
	unsigned i;

	write_le16(hdr, mix->count);
	write_le32(hdr + 2, 0);
	if (fwrite(hdr, 1, sizeof(hdr), out) != sizeof(hdr))
		return 0;

	memset(sb, 0, sizeof(sb));
	for (i = 0; i < mix->count; ++i) {
		write_le32(sb, mix->entries[i].crc);
		if (fwrite(sb, 1, sizeof(sb), out) != sizeof(sb))
			return 0;
	}
	return 1;
}

static int patch_header(FILE *out, const RemixMix *mix)
{
	unsigned char hdr[6];
	unsigned char sb[12];
	unsigned i;

	if (fseek(out, 0, SEEK_SET) != 0)
		return 0;

	write_le16(hdr, mix->count);
	write_le32(hdr + 2, mix->data_size);
	if (fwrite(hdr, 1, sizeof(hdr), out) != sizeof(hdr))
		return 0;

	for (i = 0; i < mix->count; ++i) {
		const RemixEntry *e = &mix->entries[i];
		write_le32(sb, e->crc);
		write_le32(sb + 4, e->new_offset);
		write_le32(sb + 8, e->new_size);
		if (fwrite(sb, 1, sizeof(sb), out) != sizeof(sb))
			return 0;
	}
	return 1;
}

static int process_and_write_entry(
    FILE *in, FILE *out, RemixMix *mix, unsigned index, uint32_t *body_pos)
{
	RemixEntry *e = &mix->entries[index];
	unsigned char probe[512];
	unsigned char *file_buf = NULL;
	unsigned char *converted = NULL;
	size_t probe_len;
	size_t converted_len = 0;
	long in_pos;
	int is_aud99 = 0;

	e->new_offset = 0;
	e->new_size = 0;
	snprintf(e->type_in, sizeof(e->type_in), "empty");
	snprintf(e->type_out, sizeof(e->type_out), "empty");

	if (e->old_size == 0)
		return 1;

	in_pos = (long)mix->data_start + (long)e->old_offset;
	if (fseek(in, in_pos, SEEK_SET) != 0) {
		fprintf(stderr, "error: seek failed for entry %u\n", index);
		return 0;
	}

	probe_len = e->old_size < sizeof(probe) ? e->old_size : sizeof(probe);
	if (fread(probe, 1, probe_len, in) != probe_len) {
		fprintf(stderr, "error: short read for entry %u\n", index);
		return 0;
	}

	detect_file_type(probe, probe_len, e->old_size, e->type_in, sizeof(e->type_in));
	snprintf(e->type_out, sizeof(e->type_out), "%s", e->type_in);

	if (probe_len >= (size_t)REMIX_AUD_HDR_LEN && probe[11] == REMIX_AUD_COMP_IMA99) {
		is_aud99 = 1;
	}

	if (is_aud99) {
		file_buf = (unsigned char *)malloc(e->old_size);
		if (!file_buf) {
			fprintf(stderr, "error: out of memory for entry %u\n", index);
			return 0;
		}
		memcpy(file_buf, probe, probe_len);
		if (probe_len < e->old_size) {
			if (fread(file_buf + probe_len, 1, e->old_size - probe_len, in) != e->old_size - probe_len) {
				fprintf(stderr, "error: short read for entry %u\n", index);
				free(file_buf);
				return 0;
			}
		}
		if (!remix_is_aud99(file_buf, e->old_size))
			is_aud99 = 0;
	}

	if (((mix->data_start + *body_pos) & 1u) != 0u) {
		unsigned char pad = 0;
		if (fwrite(&pad, 1, 1, out) != 1) {
			free(file_buf);
			return 0;
		}
		++(*body_pos);
	}

	e->new_offset = *body_pos;

	if (is_aud99 && remix_convert_aud99(file_buf, e->old_size, &converted, &converted_len)) {
		if (fwrite(converted, 1, converted_len, out) != converted_len) {
			free(converted);
			free(file_buf);
			return 0;
		}
		e->new_size = (uint32_t)converted_len;
		snprintf(e->type_out, sizeof(e->type_out), "aud_pcm11");
		free(converted);
		free(file_buf);
	} else {
		free(file_buf);
		file_buf = NULL;

		if (fwrite(probe, 1, probe_len, out) != probe_len)
			return 0;

		if (probe_len < e->old_size) {
			if (!stream_copy(in, out, e->old_size - probe_len))
				return 0;
		}
		e->new_size = e->old_size;
	}

	*body_pos += e->new_size;
	print_status(e);
	return 1;
}

static int remix_file(const char *in_path, const char *out_path)
{
	FILE *in = NULL;
	FILE *out = NULL;
	RemixMix mix;
	uint32_t body_pos = 0;
	unsigned i;

	memset(&mix, 0, sizeof(mix));

	in = fopen(in_path, "rb");
	if (!in) {
		fprintf(stderr, "error: cannot open %s\n", in_path);
		return 0;
	}

	if (!read_plain_mix_header(in, &mix)) {
		fclose(in);
		free_mix(&mix);
		return 0;
	}

	out = fopen(out_path, "wb");
	if (!out) {
		fprintf(stderr, "error: cannot create %s\n", out_path);
		fclose(in);
		free_mix(&mix);
		return 0;
	}

	if (!write_placeholder_header(out, &mix)) {
		fprintf(stderr, "error: write failed for %s\n", out_path);
		goto fail;
	}

	printf("%s -> %s (%u files, data at %u):\n",
	    in_path, out_path, (unsigned)mix.count, mix.data_start);
	print_table_header();

	for (i = 0; i < mix.count; ++i) {
		if (!process_and_write_entry(in, out, &mix, i, &body_pos)) {
			fprintf(stderr, "error: failed processing entry %u\n", i);
			goto fail;
		}
	}

	mix.data_size = body_pos;

	if (!patch_header(out, &mix)) {
		fprintf(stderr, "error: failed to write MIX header for %s\n", out_path);
		goto fail;
	}

	fclose(in);
	fclose(out);
	printf("wrote %s (%u files, %u data bytes)\n", out_path, (unsigned)mix.count, mix.data_size);
	free_mix(&mix);
	return 1;

fail:
	fclose(in);
	fclose(out);
	free_mix(&mix);
	return 0;
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
	    "  - convert AUD99 IMA to 11 kHz 8-bit mono PCM .AUD\n"
	    "  - pad payloads so each begins at an even offset from the MIX start\n"
	    "\n"
	    "Options:\n"
	    "  -o, --output PATH   output MIX file, or output directory with -d\n"
	    "  -d, --directory DIR remix all .mix/.MIX files in DIR (non-recursive)\n"
	    "                      updates files in place unless -o is given\n"
	    "  -h, --help          show this help\n"
	    "\n"
	    "Single-file mode requires -o to differ from input. With -d alone, each\n"
	    "MIX is rewritten via a temporary file in the same directory.\n",
	    prog, prog);
}

int main(int argc, char **argv)
{
	const char *in_path = NULL;
	const char *out_path = NULL;
	const char *in_dir = NULL;
	int argi;

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
		if (out_path) {
			if (!path_is_directory(out_path)) {
				fprintf(stderr, "error: %s is not a directory (expected output directory with -d)\n", out_path);
				return 1;
			}
			if (paths_same(in_dir, out_path)) {
				fprintf(stderr, "error: output directory must differ from input directory\n");
				return 1;
			}
		}
		return remix_directory(in_dir, out_path) ? 0 : 1;
	}

	if (!in_path || !out_path) {
		if (!out_path)
			fprintf(stderr, "error: -o output path is required\n");
		if (!in_path)
			fprintf(stderr, "error: input MIX file or -d directory is required\n");
		usage(argv[0]);
		return 1;
	}

	if (paths_same(in_path, out_path)) {
		fprintf(stderr, "error: output path must differ from input path\n");
		return 1;
	}

	return remix_file(in_path, out_path) ? 0 : 1;
}
