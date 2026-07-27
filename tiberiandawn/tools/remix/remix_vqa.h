/*
 * remix_vqa.h - VQA → STVQ conversion for remix.
 */
#ifndef REMIX_VQA_H
#define REMIX_VQA_H

#include "remix.h"

#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 1 if probe looks like FORM STVQ (already converted). */
int remix_vqa_is_stvq(const unsigned char *data, size_t len);

/**
 * Convert a VQA MIX payload to STVQ and write it to out.
 * Returns 1 on success, -1 to omit (missing W16 / encode fail), 0 on I/O error.
 */
int remix_vqa_convert_payload(
    FILE *in, long in_pos, const unsigned char *probe, size_t probe_len, uint32_t payload_size,
    uint32_t crc, const RemixConfig *cfg, FILE *out, uint32_t *out_size);

/**
 * Convert an in-memory VQA buffer to STVQ.
 * On success (*out_stv is malloc'd; caller frees). Returns 1 / -1 / 0 as above.
 */
int remix_vqa_convert_buffer(
    const unsigned char *vqa, size_t vqa_len, uint32_t crc, const RemixConfig *cfg,
    unsigned char **out_stv, uint32_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* REMIX_VQA_H */
