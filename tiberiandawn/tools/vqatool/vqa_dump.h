/*
 * vqa_dump.h - Decode VQA frames to uncompressed BMP files.
 */
#ifndef VQA_DUMP_H
#define VQA_DUMP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VqaDumpOpts {
	const char *vqa_path;
	const char *out_dir;   /* NULL → <stem>_bmp next to VQA */
	int every;             /* dump every Nth frame (1 = all); default 1 */
	int frame_first;       /* inclusive; -1 = from start */
	int frame_last;        /* inclusive; -1 = through end */
	int rgb24;             /* 1 = 24-bit BGR; 0 = 8-bit paletted (default) */
	int dry_run;
} VqaDumpOpts;

int vqa_dump(const VqaDumpOpts *opts);

#ifdef __cplusplus
}
#endif

#endif /* VQA_DUMP_H */
