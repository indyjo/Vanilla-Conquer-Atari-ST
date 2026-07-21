/*
 * vqa_io.c - POSIX byte I/O for vqatool.
 */
#include "vqa_io.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

uint16_t vqa_read_le16(const unsigned char *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

uint32_t vqa_read_le32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint32_t vqa_read_be32(const unsigned char *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

unsigned vqa_iff_data_padded(uint32_t size)
{
	return (unsigned)((size + 1u) & ~1u);
}

int vqa_reader_open(VqaReader *r, const char *path)
{
	struct stat st;

	memset(r, 0, sizeof(*r));
	r->path = path;
	r->fd = open(path, O_RDONLY);
	if (r->fd < 0) {
		fprintf(stderr, "error: %s: open: %s\n", path, strerror(errno));
		return -1;
	}
	if (fstat(r->fd, &st) != 0) {
		fprintf(stderr, "error: %s: fstat: %s\n", path, strerror(errno));
		close(r->fd);
		r->fd = -1;
		return -1;
	}
	r->size = st.st_size;
	return 0;
}

void vqa_reader_close(VqaReader *r)
{
	if (r->fd >= 0) {
		close(r->fd);
		r->fd = -1;
	}
}

off_t vqa_reader_tell(const VqaReader *r)
{
	return lseek(r->fd, 0, SEEK_CUR);
}

int vqa_reader_seek(VqaReader *r, off_t offset)
{
	if (lseek(r->fd, offset, SEEK_SET) < 0) {
		fprintf(stderr, "error: %s: seek %lld: %s\n", r->path, (long long)offset, strerror(errno));
		return -1;
	}
	return 0;
}

int vqa_reader_read(VqaReader *r, void *buf, size_t nbytes)
{
	unsigned char *out = (unsigned char *)buf;
	size_t total = 0;

	while (total < nbytes) {
		ssize_t n = read(r->fd, out + total, nbytes - total);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			fprintf(stderr, "error: %s: read: %s\n", r->path, strerror(errno));
			return -1;
		}
		if (n == 0) {
			fprintf(stderr, "error: %s: unexpected EOF\n", r->path);
			return -1;
		}
		total += (size_t)n;
	}
	return 0;
}

int vqa_reader_skip(VqaReader *r, size_t nbytes)
{
	if (nbytes == 0) {
		return 0;
	}
	if (lseek(r->fd, (off_t)nbytes, SEEK_CUR) < 0) {
		fprintf(stderr, "error: %s: skip %zu: %s\n", r->path, nbytes, strerror(errno));
		return -1;
	}
	return 0;
}

int vqa_reader_read_chunk_hdr(VqaReader *r, uint32_t *out_id, uint32_t *out_size)
{
	unsigned char hdr[8];

	if (vqa_reader_read(r, hdr, sizeof(hdr)) != 0) {
		return -1;
	}
	*out_id = vqa_read_le32(hdr);
	*out_size = vqa_read_be32(hdr + 4);
	return 0;
}
