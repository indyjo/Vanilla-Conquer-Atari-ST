/*
 * stvq_write.c - IFF writer for FORM 'STVQ'.
 */
#include "stvq_write.h"

#include <errno.h>
#include <string.h>

static int write_all(FILE *fp, const void *data, size_t n)
{
	const unsigned char *p = (const unsigned char *)data;
	size_t off = 0;
	while (off < n) {
		size_t w = fwrite(p + off, 1, n - off, fp);
		if (w == 0) {
			return -1;
		}
		off += w;
	}
	return 0;
}

int stvq_writer_open(StvqWriter *w, const char *path)
{
	memset(w, 0, sizeof(*w));
	w->fp = fopen(path, "wb");
	if (!w->fp) {
		fprintf(stderr, "error: %s: %s\n", path, strerror(errno));
		return -1;
	}
	return 0;
}

int stvq_writer_close(StvqWriter *w)
{
	int rc = 0;
	if (w->fp) {
		if (fclose(w->fp) != 0) {
			rc = -1;
		}
		w->fp = NULL;
	}
	return rc;
}

int stvq_write_chunk_begin(StvqWriter *w, uint32_t id, long *size_pos_out)
{
	unsigned char hdr[8];
	memcpy(hdr, &id, 4);
	memset(hdr + 4, 0, 4);
	if (write_all(w->fp, hdr, 8) != 0) {
		return -1;
	}
	*size_pos_out = ftell(w->fp) - 4;
	return 0;
}

int stvq_write_chunk_end(StvqWriter *w, long size_pos)
{
	long end = ftell(w->fp);
	uint32_t size;
	unsigned char be[4];
	unsigned char pad = 0;

	if (end < size_pos + 4) {
		return -1;
	}
	size = (uint32_t)(end - (size_pos + 4));
	stvq_write_be32(be, size);
	if (fseek(w->fp, size_pos, SEEK_SET) != 0) {
		return -1;
	}
	if (write_all(w->fp, be, 4) != 0) {
		return -1;
	}
	if (fseek(w->fp, end, SEEK_SET) != 0) {
		return -1;
	}
	if (size & 1u) {
		if (write_all(w->fp, &pad, 1) != 0) {
			return -1;
		}
	}
	return 0;
}

int stvq_write_chunk_raw(StvqWriter *w, uint32_t id, const void *data, uint32_t size)
{
	long size_pos;
	if (stvq_write_chunk_begin(w, id, &size_pos) != 0) {
		return -1;
	}
	if (size && write_all(w->fp, data, size) != 0) {
		return -1;
	}
	return stvq_write_chunk_end(w, size_pos);
}

int stvq_write_form_begin(StvqWriter *w)
{
	unsigned char hdr[12];
	memcpy(hdr, "FORM", 4);
	memset(hdr + 4, 0, 4);
	memcpy(hdr + 8, "STVQ", 4);
	if (write_all(w->fp, hdr, 12) != 0) {
		return -1;
	}
	w->form_size_pos = 4;
	w->form_data_start = 8;
	return 0;
}

int stvq_write_form_end(StvqWriter *w)
{
	long end = ftell(w->fp);
	uint32_t size;
	unsigned char be[4];

	if (end < w->form_data_start) {
		return -1;
	}
	size = (uint32_t)(end - w->form_data_start);
	stvq_write_be32(be, size);
	if (fseek(w->fp, w->form_size_pos, SEEK_SET) != 0) {
		return -1;
	}
	if (write_all(w->fp, be, 4) != 0) {
		return -1;
	}
	if (fseek(w->fp, end, SEEK_SET) != 0) {
		return -1;
	}
	return 0;
}
