/*
 * vqa_inspect.h - VQA file inspection for vqatool.
 */
#ifndef VQA_INSPECT_H
#define VQA_INSPECT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VqaInspectOpts {
	int show_frames;
	int show_palette_changes;
	int verbose;
} VqaInspectOpts;

int vqa_inspect_file(const char *path, const VqaInspectOpts *opts);

#ifdef __cplusplus
}
#endif

#endif /* VQA_INSPECT_H */
