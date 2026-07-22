/*
 * vqatool.c - Host utility for Westwood VQA inspection and STVQ encode/preview.
 */
#include "stvq_encode.h"
#include "stvq_fix_w16.h"
#include "stvq_format.h"
#include "stvq_init_w16.h"
#include "stvq_metric.h"
#include "stvq_preview.h"
#include "stvq_refine_w16.h"
#include "vqa_inspect.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *argv0)
{
	fprintf(stderr,
	    "usage: %s <command> [options] <file>...\n"
	    "\n"
	    "VQA inspect:\n"
	    "  inspect <file.vqa>      print VQA structure and metadata\n"
	    "    -f, --frames          list each frame container and sub-chunk summary\n"
	    "    -p, --palette         list frames where the palette changes\n"
	    "    -v, --verbose         list every chunk with file offset\n"
	    "\n"
	    "16-color W16 sidecars (next to the VQA):\n"
	    "  init-w16 <file.vqa>     write .pal/.hist; create missing .w16 via palette-opt\n"
	    "    --palette-opt PATH    palette-opt binary (required)\n"
	    "    --dry-run             print segment/sidecar plan only\n"
	    "  refine-w16 <file.vqa>   refine .w16 with palette-opt (-c if exists; else create)\n"
	    "    --palette-opt PATH    palette-opt binary (required)\n"
	    "    --normal              SA with --sa-t0=1e-4 (default)\n"
	    "    --quick               SA with --sa-t0=1.5e-5\n"
	    "    --thorough            SA with palette-opt defaults\n"
	    "    --palette-opt-args=a,b  extra palette-opt flags (comma-separated)\n"
	    "    --dry-run             print palette-opt commands only\n"
	    "  fix-w16 <file.vqa>      run w16fix on each name.<N>.w16 (in place)\n"
	    "    --w16fix PATH       w16fix binary (required)\n"
	    "    --dry-run             print w16fix commands only\n"
	    "\n"
	    "STVQ encode / preview:\n"
	    "  encode <file.vqa>       encode to FORM 'STVQ' (.stv); requires .w16 sidecars\n"
	    "    --cb-size N           codebook entries (default %u)\n"
	    "    --cb-per-frame N      max STCR replaces/frame (default %u; 0=none)\n"
	    "    --cb-random-pct N     %% of STCR slots accepted at random (default %u)\n"
	    "    --cb-lookahead N      frames ahead for eviction/utility (default %u)\n"
	    "    --gamma F             YUV gamma before DCT (default %.2g)\n"
	    "    --dct-alpha F         DCT weight alpha in 1/(1+a*(u^2+v^2)) (default %.2g)\n"
	    "    --dct-coeffs N        Y zig-zag DCT coeffs (default %u)\n"
	    "    --dct-chroma-coeffs N U and V zig-zag coeffs each (default %u; 0=Y-only)\n"
	    "    -o, --output FILE     output .stv path\n"
	    "    --dry-run             print segment/sidecar plan only\n"
	    "  preview <file.stv>      decode STVQ and pipe A/V into ffmpeg\n"
	    "    --ffmpeg PATH         ffmpeg binary (default: $FFMPEG or ffmpeg)\n"
	    "    -o, --output FILE     output .mkv path\n"
	    "\n"
	    "Not implemented:\n"
	    "  split, merge\n"
	    "\n"
	    "  -h, --help              show this help\n",
	    argv0,
	    (unsigned)STVQ_DEFAULT_CB_SIZE,
	    (unsigned)STVQ_DEFAULT_CB_PER_FRAME,
	    (unsigned)STVQ_DEFAULT_CB_RANDOM_PCT,
	    (unsigned)STVQ_DEFAULT_CB_LOOKAHEAD,
	    (double)STVQ_DEFAULT_GAMMA,
	    (double)STVQ_DEFAULT_DCT_ALPHA,
	    (unsigned)STVQ_DEFAULT_DCT_COEFFS,
	    (unsigned)STVQ_DEFAULT_DCT_CHROMA_COEFFS);
}

static int cmd_inspect(int argc, char **argv, int argi)
{
	VqaInspectOpts opts;
	int rc = 0;

	memset(&opts, 0, sizeof(opts));
	for (; argi < argc; argi++) {
		const char *a = argv[argi];
		if (strcmp(a, "-f") == 0 || strcmp(a, "--frames") == 0) {
			opts.show_frames = 1;
		} else if (strcmp(a, "-p") == 0 || strcmp(a, "--palette") == 0) {
			opts.show_palette_changes = 1;
		} else if (strcmp(a, "-v") == 0 || strcmp(a, "--verbose") == 0) {
			opts.verbose = 1;
		} else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
			usage(argv[0]);
			return 0;
		} else if (a[0] == '-') {
			fprintf(stderr, "error: unknown option: %s\n", a);
			usage(argv[0]);
			return 1;
		} else {
			break;
		}
	}
	if (argi >= argc) {
		fprintf(stderr, "error: inspect requires a VQA file path\n");
		usage(argv[0]);
		return 1;
	}
	for (; argi < argc; argi++) {
		if (vqa_inspect_file(argv[argi], &opts) != 0) {
			rc = 1;
		}
	}
	return rc;
}

static int parse_u(const char *s, unsigned *out)
{
	char *end = NULL;
	unsigned long v = strtoul(s, &end, 10);
	if (!s[0] || (end && *end) || v > 0xfffffffful)
		return -1;
	*out = (unsigned)v;
	return 0;
}

static int parse_f(const char *s, float *out)
{
	char *end = NULL;
	float v = strtof(s, &end);
	if (!s[0] || (end && *end))
		return -1;
	*out = v;
	return 0;
}

static int cmd_init_w16(int argc, char **argv, int argi)
{
	StvqInitW16Opts opts;
	memset(&opts, 0, sizeof(opts));

	for (; argi < argc; argi++) {
		const char *a = argv[argi];
		if (strcmp(a, "--palette-opt") == 0) {
			if (++argi >= argc) {
				fprintf(stderr, "error: --palette-opt needs PATH\n");
				return 1;
			}
			opts.palette_opt = argv[argi];
		} else if (!strncmp(a, "--palette-opt=", 14)) {
			opts.palette_opt = a + 14;
		} else if (strcmp(a, "--dry-run") == 0) {
			opts.dry_run = 1;
		} else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
			usage(argv[0]);
			return 0;
		} else if (a[0] == '-') {
			fprintf(stderr, "error: unknown option: %s\n", a);
			usage(argv[0]);
			return 1;
		} else {
			break;
		}
	}
	if (argi >= argc) {
		fprintf(stderr, "error: init-w16 requires a VQA file\n");
		return 1;
	}
	if (!opts.palette_opt) {
		fprintf(stderr, "error: --palette-opt PATH is required\n");
		return 1;
	}
	opts.vqa_path = argv[argi];
	return stvq_init_w16(&opts) != 0;
}

static int cmd_encode(int argc, char **argv, int argi)
{
	StvqEncodeOpts opts;
	memset(&opts, 0, sizeof(opts));
	opts.cb_size = STVQ_DEFAULT_CB_SIZE;
	opts.cb_per_frame = STVQ_DEFAULT_CB_PER_FRAME;
	opts.cb_random_pct = STVQ_DEFAULT_CB_RANDOM_PCT;
	opts.cb_lookahead = STVQ_DEFAULT_CB_LOOKAHEAD;
	opts.dct_alpha = -1.0f; /* → default */
	opts.dct_coeffs = 0;    /* → default */
	opts.gamma = -1.0f;     /* → default */

	for (; argi < argc; argi++) {
		const char *a = argv[argi];
		if (strcmp(a, "--cb-size") == 0) {
			if (++argi >= argc || parse_u(argv[argi], &opts.cb_size) != 0) {
				fprintf(stderr, "error: bad --cb-size\n");
				return 1;
			}
		} else if (!strncmp(a, "--cb-size=", 10)) {
			if (parse_u(a + 10, &opts.cb_size) != 0) {
				fprintf(stderr, "error: bad --cb-size\n");
				return 1;
			}
		} else if (strcmp(a, "--cb-per-frame") == 0) {
			if (++argi >= argc || parse_u(argv[argi], &opts.cb_per_frame) != 0) {
				fprintf(stderr, "error: bad --cb-per-frame\n");
				return 1;
			}
		} else if (!strncmp(a, "--cb-per-frame=", 15)) {
			if (parse_u(a + 15, &opts.cb_per_frame) != 0) {
				fprintf(stderr, "error: bad --cb-per-frame\n");
				return 1;
			}
		} else if (strcmp(a, "--cb-random-pct") == 0) {
			if (++argi >= argc || parse_u(argv[argi], &opts.cb_random_pct) != 0 ||
			    opts.cb_random_pct > 100u) {
				fprintf(stderr, "error: bad --cb-random-pct (0..100)\n");
				return 1;
			}
		} else if (!strncmp(a, "--cb-random-pct=", 16)) {
			if (parse_u(a + 16, &opts.cb_random_pct) != 0 || opts.cb_random_pct > 100u) {
				fprintf(stderr, "error: bad --cb-random-pct (0..100)\n");
				return 1;
			}
		} else if (strcmp(a, "--cb-lookahead") == 0) {
			if (++argi >= argc || parse_u(argv[argi], &opts.cb_lookahead) != 0) {
				fprintf(stderr, "error: bad --cb-lookahead\n");
				return 1;
			}
		} else if (!strncmp(a, "--cb-lookahead=", 15)) {
			if (parse_u(a + 15, &opts.cb_lookahead) != 0) {
				fprintf(stderr, "error: bad --cb-lookahead\n");
				return 1;
			}
		} else if (strcmp(a, "--dct-alpha") == 0) {
			if (++argi >= argc || parse_f(argv[argi], &opts.dct_alpha) != 0 || opts.dct_alpha < 0.0f) {
				fprintf(stderr, "error: bad --dct-alpha\n");
				return 1;
			}
		} else if (!strncmp(a, "--dct-alpha=", 12)) {
			if (parse_f(a + 12, &opts.dct_alpha) != 0 || opts.dct_alpha < 0.0f) {
				fprintf(stderr, "error: bad --dct-alpha\n");
				return 1;
			}
		} else if (strcmp(a, "--dct-coeffs") == 0) {
			if (++argi >= argc || parse_u(argv[argi], &opts.dct_coeffs) != 0 || opts.dct_coeffs < 1u ||
			    opts.dct_coeffs > STVQ_METRIC_MAX_COEFFS) {
				fprintf(stderr, "error: bad --dct-coeffs (1..%u)\n", (unsigned)STVQ_METRIC_MAX_COEFFS);
				return 1;
			}
		} else if (!strncmp(a, "--dct-coeffs=", 13)) {
			if (parse_u(a + 13, &opts.dct_coeffs) != 0 || opts.dct_coeffs < 1u ||
			    opts.dct_coeffs > STVQ_METRIC_MAX_COEFFS) {
				fprintf(stderr, "error: bad --dct-coeffs (1..%u)\n", (unsigned)STVQ_METRIC_MAX_COEFFS);
				return 1;
			}
		} else if (strcmp(a, "--dct-chroma-coeffs") == 0) {
			if (++argi >= argc || parse_u(argv[argi], &opts.dct_chroma_coeffs) != 0 ||
			    opts.dct_chroma_coeffs > STVQ_METRIC_MAX_COEFFS) {
				fprintf(stderr, "error: bad --dct-chroma-coeffs (0..%u)\n",
				    (unsigned)STVQ_METRIC_MAX_COEFFS);
				return 1;
			}
			opts.have_dct_chroma = 1;
		} else if (!strncmp(a, "--dct-chroma-coeffs=", 20)) {
			if (parse_u(a + 20, &opts.dct_chroma_coeffs) != 0 ||
			    opts.dct_chroma_coeffs > STVQ_METRIC_MAX_COEFFS) {
				fprintf(stderr, "error: bad --dct-chroma-coeffs (0..%u)\n",
				    (unsigned)STVQ_METRIC_MAX_COEFFS);
				return 1;
			}
			opts.have_dct_chroma = 1;
		} else if (strcmp(a, "--gamma") == 0) {
			if (++argi >= argc || parse_f(argv[argi], &opts.gamma) != 0 || opts.gamma < 0.0f) {
				fprintf(stderr, "error: bad --gamma\n");
				return 1;
			}
		} else if (!strncmp(a, "--gamma=", 8)) {
			if (parse_f(a + 8, &opts.gamma) != 0 || opts.gamma < 0.0f) {
				fprintf(stderr, "error: bad --gamma\n");
				return 1;
			}
		} else if (strcmp(a, "-o") == 0 || strcmp(a, "--output") == 0) {
			if (++argi >= argc) {
				fprintf(stderr, "error: -o needs FILE\n");
				return 1;
			}
			opts.out_path = argv[argi];
		} else if (!strncmp(a, "--output=", 9)) {
			opts.out_path = a + 9;
		} else if (strcmp(a, "--dry-run") == 0) {
			opts.dry_run = 1;
		} else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
			usage(argv[0]);
			return 0;
		} else if (a[0] == '-') {
			fprintf(stderr, "error: unknown option: %s\n", a);
			usage(argv[0]);
			return 1;
		} else {
			break;
		}
	}
	if (argi >= argc) {
		fprintf(stderr, "error: encode requires a VQA file\n");
		return 1;
	}
	opts.vqa_path = argv[argi];
	return stvq_encode(&opts) != 0;
}

static int cmd_preview(int argc, char **argv, int argi)
{
	StvqPreviewOpts opts;
	memset(&opts, 0, sizeof(opts));

	for (; argi < argc; argi++) {
		const char *a = argv[argi];
		if (strcmp(a, "--ffmpeg") == 0) {
			if (++argi >= argc) {
				fprintf(stderr, "error: --ffmpeg needs PATH\n");
				return 1;
			}
			opts.ffmpeg = argv[argi];
		} else if (!strncmp(a, "--ffmpeg=", 9)) {
			opts.ffmpeg = a + 9;
		} else if (strcmp(a, "-o") == 0 || strcmp(a, "--output") == 0) {
			if (++argi >= argc) {
				fprintf(stderr, "error: -o needs FILE\n");
				return 1;
			}
			opts.out_path = argv[argi];
		} else if (!strncmp(a, "--output=", 9)) {
			opts.out_path = a + 9;
		} else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
			usage(argv[0]);
			return 0;
		} else if (a[0] == '-') {
			fprintf(stderr, "error: unknown option: %s\n", a);
			return 1;
		} else {
			break;
		}
	}
	if (argi >= argc) {
		fprintf(stderr, "error: preview requires an .stv file\n");
		return 1;
	}
	opts.stv_path = argv[argi];
	return stvq_preview(&opts) != 0;
}

static int cmd_refine_w16(int argc, char **argv, int argi)
{
	StvqRefineW16Opts opts;
	int mode_set = 0;
	memset(&opts, 0, sizeof(opts));
	opts.mode = STVQ_REFINE_NORMAL;

	for (; argi < argc; argi++) {
		const char *a = argv[argi];
		if (strcmp(a, "--palette-opt") == 0) {
			if (++argi >= argc) {
				fprintf(stderr, "error: --palette-opt needs PATH\n");
				return 1;
			}
			opts.palette_opt = argv[argi];
		} else if (!strncmp(a, "--palette-opt=", 14)) {
			opts.palette_opt = a + 14;
		} else if (strcmp(a, "--normal") == 0 || strcmp(a, "--quick") == 0 ||
		           strcmp(a, "--thorough") == 0) {
			StvqRefineMode m = STVQ_REFINE_NORMAL;
			if (strcmp(a, "--quick") == 0)
				m = STVQ_REFINE_QUICK;
			else if (strcmp(a, "--thorough") == 0)
				m = STVQ_REFINE_THOROUGH;
			if (mode_set && opts.mode != m) {
				fprintf(stderr, "error: --normal, --quick, and --thorough are mutually exclusive\n");
				return 1;
			}
			opts.mode = m;
			mode_set = 1;
		} else if (strcmp(a, "--palette-opt-args") == 0) {
			if (++argi >= argc) {
				fprintf(stderr, "error: --palette-opt-args needs VALUE\n");
				return 1;
			}
			opts.palette_opt_args = argv[argi];
		} else if (!strncmp(a, "--palette-opt-args=", 19)) {
			opts.palette_opt_args = a + 19;
		} else if (strcmp(a, "--dry-run") == 0) {
			opts.dry_run = 1;
		} else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
			usage(argv[0]);
			return 0;
		} else if (a[0] == '-') {
			fprintf(stderr, "error: unknown option: %s\n", a);
			usage(argv[0]);
			return 1;
		} else {
			break;
		}
	}
	if (argi >= argc) {
		fprintf(stderr, "error: refine-w16 requires a VQA file\n");
		return 1;
	}
	if (!opts.palette_opt) {
		fprintf(stderr, "error: --palette-opt PATH is required\n");
		return 1;
	}
	opts.vqa_path = argv[argi];
	return stvq_refine_w16(&opts) != 0;
}

static int cmd_fix_w16(int argc, char **argv, int argi)
{
	StvqFixW16Opts opts;
	memset(&opts, 0, sizeof(opts));

	for (; argi < argc; argi++) {
		const char *a = argv[argi];
		if (strcmp(a, "--w16fix") == 0) {
			if (++argi >= argc) {
				fprintf(stderr, "error: --w16fix needs PATH\n");
				return 1;
			}
			opts.w16fix = argv[argi];
		} else if (!strncmp(a, "--w16fix=", 10)) {
			opts.w16fix = a + 10;
		} else if (strcmp(a, "--dry-run") == 0) {
			opts.dry_run = 1;
		} else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
			usage(argv[0]);
			return 0;
		} else if (a[0] == '-') {
			fprintf(stderr, "error: unknown option: %s\n", a);
			usage(argv[0]);
			return 1;
		} else {
			break;
		}
	}
	if (argi >= argc) {
		fprintf(stderr, "error: fix-w16 requires a VQA file\n");
		return 1;
	}
	if (!opts.w16fix) {
		fprintf(stderr, "error: --w16fix PATH is required\n");
		return 1;
	}
	opts.vqa_path = argv[argi];
	return stvq_fix_w16(&opts) != 0;
}

static int cmd_not_implemented(const char *name)
{
	fprintf(stderr, "error: '%s' is not implemented yet\n", name);
	return 1;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		usage(argv[0]);
		return 1;
	}

	if (strcmp(argv[1], "inspect") == 0) {
		return cmd_inspect(argc, argv, 2);
	}
	if (strcmp(argv[1], "init-w16") == 0) {
		return cmd_init_w16(argc, argv, 2);
	}
	if (strcmp(argv[1], "encode") == 0) {
		return cmd_encode(argc, argv, 2);
	}
	if (strcmp(argv[1], "preview") == 0) {
		return cmd_preview(argc, argv, 2);
	}
	if (strcmp(argv[1], "refine-w16") == 0) {
		return cmd_refine_w16(argc, argv, 2);
	}
	if (strcmp(argv[1], "fix-w16") == 0) {
		return cmd_fix_w16(argc, argv, 2);
	}
	if (strcmp(argv[1], "split") == 0) {
		return cmd_not_implemented("split");
	}
	if (strcmp(argv[1], "merge") == 0) {
		return cmd_not_implemented("merge");
	}
	if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
		usage(argv[0]);
		return 0;
	}

	fprintf(stderr, "error: unknown command: %s\n", argv[1]);
	usage(argv[0]);
	return 1;
}
