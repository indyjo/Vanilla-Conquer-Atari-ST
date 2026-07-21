/*
 * stvq_write.h - IFF writer for FORM 'STVQ'.
 */
#ifndef STVQ_WRITE_H
#define STVQ_WRITE_H

#include "stvq_format.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct StvqWriter {
	FILE *fp;
	long form_size_pos;
	long form_data_start;
} StvqWriter;

int stvq_writer_open(StvqWriter *w, const char *path);
int stvq_writer_close(StvqWriter *w);

int stvq_write_chunk_begin(StvqWriter *w, uint32_t id, long *size_pos_out);
int stvq_write_chunk_end(StvqWriter *w, long size_pos);
int stvq_write_chunk_raw(StvqWriter *w, uint32_t id, const void *data, uint32_t size);

int stvq_write_form_begin(StvqWriter *w);
int stvq_write_form_end(StvqWriter *w);

#ifdef __cplusplus
}
#endif

#endif /* STVQ_WRITE_H */
