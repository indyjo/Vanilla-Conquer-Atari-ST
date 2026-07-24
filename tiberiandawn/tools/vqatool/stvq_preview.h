/*
 * stvq_preview.h - Pipe STVQ decode into ffmpeg (A/V, no fifos).
 */
#ifndef STVQ_PREVIEW_H
#define STVQ_PREVIEW_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct StvqPreviewOpts {
	const char *stv_path;
	const char *out_path;
	const char *ffmpeg; /* NULL → FFMPEG env → "ffmpeg" */
	int show_video;     /* default 1; cleared by --no-video */
	int show_palette;   /* --palette */
	int show_codebook;  /* --codebook */
	unsigned cb_border; /* pixels around each codebook tile; default 0 */
} StvqPreviewOpts;

int stvq_preview(const StvqPreviewOpts *opts);

#ifdef __cplusplus
}
#endif

#endif /* STVQ_PREVIEW_H */
