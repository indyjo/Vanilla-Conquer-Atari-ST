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
} StvqPreviewOpts;

int stvq_preview(const StvqPreviewOpts *opts);

#ifdef __cplusplus
}
#endif

#endif /* STVQ_PREVIEW_H */
