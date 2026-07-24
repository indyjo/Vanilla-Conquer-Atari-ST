/*
 * w16sort.c - Reorder C2P .W16 subset pens by YUV Hamiltonian path.
 *
 * Loads FILE.W16 and an accompanying 768-byte VGA .PAL. Converts the 16
 * subset colors to YUV (linear 6-bit DAC, Y scaled by 2). Pen 0 gets the
 * darkest color (lowest Y), pen 15 the brightest; pens 1..14 follow the
 * shortest Hamiltonian path between those endpoints (exact Held-Karp).
 * Weight columns are permuted to match.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W16_MAGIC "W16"
#define W16_FILE_BYTES (4 + 16 + 256 * 16)
#define PAL_BYTES 768
#define N 16
#define FULL_MASK ((1u << N) - 1u)
#define Y_SCALE 2.0f
#define INF 1.0e30f

typedef struct {
	unsigned char subset[N];
	unsigned char weights[256][N];
} W16Data;

typedef struct {
	float yuv[N][3];
	float y[N];
} SubsetYuv;

static int subset_has_dup(const unsigned char *s, int n)
{
	int i, j;
	for (i = 0; i < n; i++) {
		for (j = i + 1; j < n; j++) {
			if (s[i] == s[j])
				return 1;
		}
	}
	return 0;
}

static int load_w16(const char *path, W16Data *out)
{
	unsigned char hdr[4 + N];
	FILE *f;
	size_t n;

	f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "error: cannot open %s\n", path);
		return 0;
	}
	n = fread(hdr, 1, sizeof(hdr), f);
	if (n != sizeof(hdr)) {
		fprintf(stderr, "error: short read in %s\n", path);
		fclose(f);
		return 0;
	}
	if (memcmp(hdr, W16_MAGIC, 4) != 0) {
		fprintf(stderr, "error: %s: bad magic (expected W16)\n", path);
		fclose(f);
		return 0;
	}
	memcpy(out->subset, hdr + 4, N);
	for (int src = 0; src < 256; src++) {
		if (fread(out->weights[src], 1, N, f) != N) {
			fprintf(stderr, "error: short read weights row %d in %s\n", src, path);
			fclose(f);
			return 0;
		}
	}
	fclose(f);
	if (subset_has_dup(out->subset, N)) {
		fprintf(stderr, "error: subset in %s contains duplicate indices\n", path);
		return 0;
	}
	for (int src = 0; src < 256; src++) {
		int sum = 0;
		for (int k = 0; k < N; k++)
			sum += (int)out->weights[src][k];
		if (sum != N) {
			fprintf(stderr, "error: weights row %d sums to %d (expected %d)\n", src, sum, N);
			return 0;
		}
	}
	return 1;
}

static int save_w16(const char *path, const W16Data *data)
{
	FILE *f;

	f = fopen(path, "wb");
	if (!f) {
		fprintf(stderr, "error: cannot write %s\n", path);
		return 0;
	}
	if (fwrite(W16_MAGIC, 1, 4, f) != 4 || fwrite(data->subset, 1, N, f) != N) {
		fprintf(stderr, "error: cannot write header to %s\n", path);
		fclose(f);
		return 0;
	}
	for (int src = 0; src < 256; src++) {
		if (fwrite(data->weights[src], 1, N, f) != N) {
			fprintf(stderr, "error: cannot write weights row %d to %s\n", src, path);
			fclose(f);
			return 0;
		}
	}
	fclose(f);
	return 1;
}

static int load_pal(const char *path, unsigned char pal[PAL_BYTES])
{
	FILE *f;
	size_t n;

	f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "error: cannot open %s\n", path);
		return 0;
	}
	n = fread(pal, 1, PAL_BYTES, f);
	fclose(f);
	if (n != PAL_BYTES) {
		fprintf(stderr, "error: %s: expected %d-byte palette, got %zu\n", path, PAL_BYTES, n);
		return 0;
	}
	return 1;
}

/* Replace final extension with .pal; if none, append .pal. */
static int derive_pal_path(const char *w16_path, char *out, size_t out_sz)
{
	const char *slash;
	const char *dot;
	size_t stem_len;

	if (!w16_path || !out || out_sz < 5)
		return 0;

	slash = strrchr(w16_path, '/');
#ifdef _WIN32
	{
		const char *b = strrchr(w16_path, '\\');
		if (b && (!slash || b > slash))
			slash = b;
	}
#endif
	dot = strrchr(w16_path, '.');
	if (dot && (!slash || dot > slash))
		stem_len = (size_t)(dot - w16_path);
	else
		stem_len = strlen(w16_path);

	if (stem_len + 4 >= out_sz)
		return 0;
	memcpy(out, w16_path, stem_len);
	memcpy(out + stem_len, ".pal", 5);
	return 1;
}

static void subset_to_yuv(const unsigned char pal[PAL_BYTES], const unsigned char subset[N],
	SubsetYuv *out)
{
	int i;

	for (i = 0; i < N; i++) {
		const int pi = (int)subset[i];
		const float r = (float)(pal[3 * pi + 0] & 63) / 63.0f;
		const float g = (float)(pal[3 * pi + 1] & 63) / 63.0f;
		const float b = (float)(pal[3 * pi + 2] & 63) / 63.0f;
		const float y = 0.299f * r + 0.587f * g + 0.114f * b;
		const float u = 0.492f * (b - y);
		const float v = 0.877f * (r - y);

		out->yuv[i][0] = Y_SCALE * y;
		out->yuv[i][1] = u;
		out->yuv[i][2] = v;
		out->y[i] = y;
	}
}

static float dist3(const float *a, const float *b)
{
	const float dx = a[0] - b[0];
	const float dy = a[1] - b[1];
	const float dz = a[2] - b[2];
	return sqrtf(dx * dx + dy * dy + dz * dz);
}

static int old_pen_for_palette(const unsigned char *subset, int pal)
{
	int k;
	for (k = 0; k < N; k++) {
		if ((int)subset[k] == pal)
			return k;
	}
	return -1;
}

static void log_subset(FILE *log, const char *label, const unsigned char *subset)
{
	int i;
	fprintf(log, "%s:", label);
	for (i = 0; i < N; i++)
		fprintf(log, " %u", (unsigned)subset[i]);
	fputc('\n', log);
}

/*
 * Exact shortest Hamiltonian path on N nodes with fixed endpoints.
 * order[k] = local subset index visited at pen k.
 */
static int shortest_path_fixed_ends(const float dist[N][N], int start, int end, int order[N])
{
	static float dp[1u << N][N];
	static int parent[1u << N][N];
	unsigned mask;
	int v, u, cur, idx;

	for (mask = 0; mask <= FULL_MASK; mask++) {
		for (v = 0; v < N; v++) {
			dp[mask][v] = INF;
			parent[mask][v] = -1;
		}
	}

	dp[1u << start][start] = 0.0f;

	for (mask = 0; mask <= FULL_MASK; mask++) {
		for (v = 0; v < N; v++) {
			if (!(mask & (1u << v)) || !(dp[mask][v] < INF))
				continue;
			for (u = 0; u < N; u++) {
				unsigned nm;
				float nd;

				if (mask & (1u << u))
					continue;
				nm = mask | (1u << u);
				nd = dp[mask][v] + dist[v][u];
				if (nd < dp[nm][u]) {
					dp[nm][u] = nd;
					parent[nm][u] = v;
				}
			}
		}
	}

	if (!(dp[FULL_MASK][end] < INF)) {
		fprintf(stderr, "error: no Hamiltonian path from dark to bright\n");
		return 0;
	}

	cur = end;
	mask = FULL_MASK;
	idx = N - 1;
	while (idx >= 0) {
		int p;

		if (cur < 0) {
			fprintf(stderr, "error: path reconstruction failed\n");
			return 0;
		}
		order[idx] = cur;
		p = parent[mask][cur];
		mask ^= 1u << cur;
		cur = p;
		idx--;
	}

	if (order[0] != start || order[N - 1] != end) {
		fprintf(stderr, "error: path endpoints mismatch\n");
		return 0;
	}
	return 1;
}

static int sort_w16(W16Data *io, const unsigned char pal[PAL_BYTES])
{
	SubsetYuv sy;
	float dist[N][N];
	int order[N];
	unsigned char new_subset[N];
	unsigned char new_weights[256][N];
	int dark, bright;
	int i, j;

	subset_to_yuv(pal, io->subset, &sy);

	dark = 0;
	bright = 0;
	for (i = 1; i < N; i++) {
		if (sy.y[i] < sy.y[dark])
			dark = i;
		if (sy.y[i] > sy.y[bright])
			bright = i;
	}
	if (dark == bright) {
		/* All Y equal (or single extreme); pick any other end. */
		bright = (dark + 1) % N;
	}

	for (i = 0; i < N; i++) {
		dist[i][i] = 0.0f;
		for (j = i + 1; j < N; j++) {
			const float d = dist3(sy.yuv[i], sy.yuv[j]);
			dist[i][j] = d;
			dist[j][i] = d;
		}
	}

	fprintf(stderr, "  dark  pen0  <- palette %3u (Y=%.4f)\n",
		(unsigned)io->subset[dark], sy.y[dark]);
	fprintf(stderr, "  bright pen15 <- palette %3u (Y=%.4f)\n",
		(unsigned)io->subset[bright], sy.y[bright]);

	if (!shortest_path_fixed_ends(dist, dark, bright, order))
		return 0;

	for (i = 0; i < N; i++)
		new_subset[i] = io->subset[order[i]];

	for (int src = 0; src < 256; src++) {
		for (int new_pen = 0; new_pen < N; new_pen++) {
			const int pal_idx = (int)new_subset[new_pen];
			const int old_pen = old_pen_for_palette(io->subset, pal_idx);
			if (old_pen < 0) {
				fprintf(stderr, "error: palette %d missing from old subset\n", pal_idx);
				return 0;
			}
			new_weights[src][new_pen] = io->weights[src][old_pen];
		}
	}

	log_subset(stderr, "old subset", io->subset);
	memcpy(io->subset, new_subset, N);
	memcpy(io->weights, new_weights, sizeof(new_weights));
	log_subset(stderr, "new subset", io->subset);
	fprintf(stderr, "path:");
	for (i = 0; i < N; i++)
		fprintf(stderr, " %d", order[i]);
	fputc('\n', stderr);
	return 1;
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [-o OUT] [-p PAL] FILE.W16\n"
		"\n"
		"Reorder subset pens along the shortest YUV Hamiltonian path from the\n"
		"darkest color (pen 0) to the brightest (pen 15). Weight columns are\n"
		"permuted to match. YUV uses linear VGA 6-bit DAC with Y scaled by 2.\n"
		"\n"
		"Without -p, PAL defaults to FILE with extension replaced by .pal.\n"
		"Without -o, FILE.W16 is updated in place.\n",
		prog);
}

int main(int argc, char **argv)
{
	const char *in_path = NULL;
	const char *out_path = NULL;
	const char *pal_path = NULL;
	char pal_buf[1024];
	unsigned char pal[PAL_BYTES];
	W16Data data;
	int argi;

	for (argi = 1; argi < argc; argi++) {
		if (!strcmp(argv[argi], "-o") || !strcmp(argv[argi], "--output")) {
			if (argi + 1 >= argc) {
				fprintf(stderr, "error: %s requires a path\n", argv[argi]);
				return 1;
			}
			out_path = argv[++argi];
		} else if (!strcmp(argv[argi], "-p") || !strcmp(argv[argi], "--palette")) {
			if (argi + 1 >= argc) {
				fprintf(stderr, "error: %s requires a path\n", argv[argi]);
				return 1;
			}
			pal_path = argv[++argi];
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

	if (!in_path) {
		usage(argv[0]);
		return 1;
	}
	if (!out_path)
		out_path = in_path;
	if (!pal_path) {
		if (!derive_pal_path(in_path, pal_buf, sizeof(pal_buf))) {
			fprintf(stderr, "error: cannot derive .pal path from %s\n", in_path);
			return 1;
		}
		pal_path = pal_buf;
	}

	if (!load_w16(in_path, &data))
		return 1;
	if (!load_pal(pal_path, pal))
		return 1;

	fprintf(stderr, "%s (palette %s):\n", in_path, pal_path);
	if (!sort_w16(&data, pal))
		return 1;

	if (!save_w16(out_path, &data))
		return 1;
	fprintf(stderr, "wrote %s (%d bytes)\n", out_path, W16_FILE_BYTES);
	return 0;
}
