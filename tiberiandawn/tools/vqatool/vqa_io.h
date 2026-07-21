/*
 * vqa_io.h - POSIX byte I/O for vqatool.
 */
#ifndef VQA_IO_H
#define VQA_IO_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VqaReader {
	int fd;
	const char *path;
	off_t size;
} VqaReader;

int vqa_reader_open(VqaReader *r, const char *path);
void vqa_reader_close(VqaReader *r);
off_t vqa_reader_tell(const VqaReader *r);
int vqa_reader_seek(VqaReader *r, off_t offset);
int vqa_reader_read(VqaReader *r, void *buf, size_t nbytes);
int vqa_reader_skip(VqaReader *r, size_t nbytes);

uint16_t vqa_read_le16(const unsigned char *p);
uint32_t vqa_read_le32(const unsigned char *p);
uint32_t vqa_read_be32(const unsigned char *p);

unsigned vqa_iff_data_padded(uint32_t size);
int vqa_reader_read_chunk_hdr(VqaReader *r, uint32_t *out_id, uint32_t *out_size);

#ifdef __cplusplus
}
#endif

#endif /* VQA_IO_H */
