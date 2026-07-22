/*
 * stvq_play.c - Host STVQ decode to RGB24 + PCM.
 */
#include "stvq_play.h"

#include "stvq_c2p.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static int read_fully(FILE *fp, void *buf, size_t n)
{
	unsigned char *p = (unsigned char *)buf;
	size_t got = 0;
	while (got < n) {
		size_t r = fread(p + got, 1, n - got, fp);
		if (r == 0)
			return -1;
		got += r;
	}
	return 0;
}

static int read_chunk_hdr(FILE *fp, uint32_t *id, uint32_t *size)
{
	unsigned char hdr[8];
	if (read_fully(fp, hdr, 8) != 0)
		return -1;
	memcpy(id, hdr, 4);
	*size = stvq_read_be32(hdr + 4);
	return 0;
}

static unsigned padded(uint32_t size)
{
	return (unsigned)((size + 1u) & ~1u);
}

static int skip_pad(FILE *fp, uint32_t size)
{
	unsigned pad = padded(size);
	if (pad > size) {
		unsigned char b;
		if (read_fully(fp, &b, 1) != 0)
			return -1;
	}
	return 0;
}

static void tiles_to_rgb(const StvqPlayer *p, const uint8_t *tiles, uint8_t *rgb)
{
	unsigned col, row;
	uint8_t pens[64];
	unsigned w = p->hdr.width;
	unsigned h = p->hdr.height;

	memset(rgb, 0, (size_t)w * h * 3u);
	for (col = 0; col < p->tiles_x; col++) {
		for (row = 0; row < p->tiles_y; row++) {
			const uint8_t *tile = tiles + ((size_t)col * p->tiles_y + row) * 32u;
			int ly, lx;
			stvq_unpack_tile_32(tile, pens);
			for (ly = 0; ly < 8; ly++) {
				for (lx = 0; lx < 8; lx++) {
					unsigned x = col * 8u + (unsigned)lx;
					unsigned y = row * 8u + (unsigned)ly;
					uint8_t pen, r, g, b;
					uint8_t *dst;
					if (x >= w || y >= h)
						continue;
					pen = pens[ly * 8 + lx] & 15u;
					stvq_ste_to_rgb24(p->palette[pen], &r, &g, &b);
					dst = rgb + ((size_t)y * w + x) * 3u;
					dst[0] = r;
					dst[1] = g;
					dst[2] = b;
				}
			}
		}
	}
}

static int load_stpl(StvqPlayer *p, uint32_t size)
{
	unsigned char buf[32];
	int i;
	if (size != 32 || read_fully(p->fp, buf, 32) != 0)
		return -1;
	for (i = 0; i < 16; i++)
		p->palette[i] = stvq_read_be16(buf + i * 2);
	return skip_pad(p->fp, size);
}

static int load_stcb(StvqPlayer *p, uint32_t size)
{
	size_t need = (size_t)p->hdr.cb_entries * 32u;
	if (size != need)
		return -1;
	free(p->codebook);
	p->codebook = (uint8_t *)malloc(need);
	if (!p->codebook)
		return -1;
	if (read_fully(p->fp, p->codebook, need) != 0)
		return -1;
	return skip_pad(p->fp, size);
}

static int apply_stcr(StvqPlayer *p, uint32_t size)
{
	unsigned n, i;
	if (size % 34u)
		return -1;
	n = size / 34u;
	for (i = 0; i < n; i++) {
		unsigned char ent[34];
		uint16_t idx;
		if (read_fully(p->fp, ent, 34) != 0)
			return -1;
		idx = stvq_read_be16(ent);
		if (idx >= p->hdr.cb_entries)
			return -1;
		memcpy(p->codebook + (size_t)idx * 32u, ent + 2, 32);
	}
	return skip_pad(p->fp, size);
}

static int decode_stvd(StvqPlayer *p, uint32_t size, uint8_t *out_tiles)
{
	unsigned char *body = (unsigned char *)malloc(size ? size : 1);
	const unsigned char *rp;
	unsigned col, rem;
	uint8_t *prev;

	if (!body)
		return -1;
	if (size && read_fully(p->fp, body, size) != 0) {
		free(body);
		return -1;
	}
	if (skip_pad(p->fp, size) != 0) {
		free(body);
		return -1;
	}

	prev = (p->frame_index >= 2) ? p->tiles[1] : NULL;
	rp = body;
	rem = size;

	for (col = 0; col < p->tiles_x; col++) {
		uint32_t mask;
		unsigned row;
		uint8_t *col_out = out_tiles + (size_t)col * p->tiles_y * 32u;
		const uint8_t *col_prev = prev ? prev + (size_t)col * p->tiles_y * 32u : NULL;

		if (rem < 4) {
			free(body);
			return -1;
		}
		mask = stvq_read_be32(rp);
		rp += 4;
		rem -= 4;

		for (row = 0; row < p->tiles_y; row++) {
			uint32_t sum = mask + mask;
			int is_skip = (sum < mask);
			uint8_t *dst = col_out + row * 32u;
			mask = sum;
			if (is_skip) {
				if (!col_prev) {
					free(body);
					return -1;
				}
				memcpy(dst, col_prev + row * 32u, 32);
			} else {
				uint16_t idx;
				if (rem < 2) {
					free(body);
					return -1;
				}
				idx = stvq_read_be16(rp);
				rp += 2;
				rem -= 2;
				if (idx >= p->hdr.cb_entries) {
					free(body);
					return -1;
				}
				memcpy(dst, p->codebook + (size_t)idx * 32u, 32);
			}
		}
	}

	free(body);
	return 0;
}

void stvq_player_close(StvqPlayer *p)
{
	if (p->fp) {
		fclose(p->fp);
		p->fp = NULL;
	}
	free(p->codebook);
	free(p->tiles[0]);
	free(p->tiles[1]);
	free(p->work_tiles);
	free(p->rgb);
	free(p->pcm);
	memset(p, 0, sizeof(*p));
}

int stvq_player_open(StvqPlayer *p, const char *path)
{
	uint32_t id, size;
	unsigned char raw[STVQ_STHD_SIZE];
	int have_sthd = 0, have_stpl = 0, have_stcb = 0;

	memset(p, 0, sizeof(*p));
	p->fp = fopen(path, "rb");
	if (!p->fp) {
		fprintf(stderr, "error: %s: %s\n", path, strerror(errno));
		return -1;
	}

	if (read_chunk_hdr(p->fp, &id, &size) != 0 || id != STVQ_CHUNK_FORM)
		goto fail;
	if (read_fully(p->fp, &id, 4) != 0 || id != STVQ_CHUNK_STVQ)
		goto fail;

	while (!have_sthd || !have_stpl || !have_stcb) {
		long pos = ftell(p->fp);
		if (read_chunk_hdr(p->fp, &id, &size) != 0)
			goto fail;
		if (id == STVQ_CHUNK_STHD) {
			if (size != STVQ_STHD_SIZE || read_fully(p->fp, raw, STVQ_STHD_SIZE) != 0)
				goto fail;
			stvq_header_unpack(raw, &p->hdr);
			if (skip_pad(p->fp, size) != 0)
				goto fail;
			have_sthd = 1;
			p->tiles_x = stvq_tiles_x(p->hdr.width);
			p->tiles_y = stvq_tiles_y(p->hdr.height);
			p->tiles_n = p->tiles_x * p->tiles_y;
		} else if (id == STVQ_CHUNK_STPL) {
			if (load_stpl(p, size) != 0)
				goto fail;
			have_stpl = 1;
		} else if (id == STVQ_CHUNK_STCB) {
			if (!have_sthd)
				goto fail;
			if (load_stcb(p, size) != 0)
				goto fail;
			have_stcb = 1;
		} else if (id == STVQ_CHUNK_STFR) {
			/* rewind to STFR for playback loop */
			if (fseek(p->fp, pos, SEEK_SET) != 0)
				goto fail;
			break;
		} else {
			unsigned char *skip = (unsigned char *)malloc(padded(size));
			if (!skip && size)
				goto fail;
			if (size && read_fully(p->fp, skip, padded(size)) != 0) {
				free(skip);
				goto fail;
			}
			free(skip);
		}
	}

	if (!have_sthd || !have_stpl || !have_stcb) {
		fprintf(stderr, "error: incomplete STVQ header (need STHD/STPL/STCB)\n");
		goto fail;
	}
	if (!p->hdr.fps)
		p->hdr.fps = 15;
	if (!p->hdr.sample_rate)
		p->hdr.sample_rate = (uint16_t)STVQ_SAMPLE_RATE;

	p->tiles[0] = (uint8_t *)calloc(p->tiles_n, 32u);
	p->tiles[1] = (uint8_t *)calloc(p->tiles_n, 32u);
	p->work_tiles = (uint8_t *)calloc(p->tiles_n, 32u);
	p->rgb = (uint8_t *)malloc((size_t)p->hdr.width * p->hdr.height * 3u);
	if (!p->tiles[0] || !p->tiles[1] || !p->work_tiles || !p->rgb)
		goto fail;
	p->frame_index = 0;
	return 0;

fail:
	stvq_player_close(p);
	return -1;
}

int stvq_player_next_frame(StvqPlayer *p)
{
	uint32_t id, size;
	unsigned consumed;
	int got_stvd = 0;

	if (p->eof)
		return 0;

	if (read_chunk_hdr(p->fp, &id, &size) != 0)
		return -1;
	if (id == STVQ_CHUNK_STEN) {
		p->eof = 1;
		return 0;
	}
	if (id != STVQ_CHUNK_STFR) {
		fprintf(stderr, "error: expected STFR, got %.4s\n", (char *)&id);
		return -1;
	}

	p->pcm_len = 0;
	consumed = 0;
	while (consumed < size) {
		uint32_t cid, csize;
		unsigned cpad;
		long cpos = ftell(p->fp);
		if (read_chunk_hdr(p->fp, &cid, &csize) != 0)
			return -1;
		cpad = padded(csize);
		consumed += 8u + cpad;

		if (cid == STVQ_CHUNK_STPL) {
			if (load_stpl(p, csize) != 0)
				return -1;
		} else if (cid == STVQ_CHUNK_STCR) {
			if (apply_stcr(p, csize) != 0)
				return -1;
		} else if (cid == STVQ_CHUNK_STVD) {
			if (fseek(p->fp, cpos, SEEK_SET) != 0)
				return -1;
			if (read_chunk_hdr(p->fp, &cid, &csize) != 0)
				return -1;
			if (decode_stvd(p, csize, p->work_tiles) != 0)
				return -1;
			got_stvd = 1;
		} else if (cid == STVQ_CHUNK_SND0) {
			if (csize > p->pcm_cap) {
				uint8_t *nbuf = (uint8_t *)realloc(p->pcm, csize);
				if (!nbuf)
					return -1;
				p->pcm = nbuf;
				p->pcm_cap = csize;
			}
			if (csize && read_fully(p->fp, p->pcm, csize) != 0)
				return -1;
			p->pcm_len = csize;
			if (skip_pad(p->fp, csize) != 0)
				return -1;
		} else {
			unsigned char *skip = (unsigned char *)malloc(cpad ? cpad : 1);
			if (!skip)
				return -1;
			if (cpad && read_fully(p->fp, skip, cpad) != 0) {
				free(skip);
				return -1;
			}
			free(skip);
		}
	}

	if (!got_stvd) {
		fprintf(stderr, "error: STFR missing STVD\n");
		return -1;
	}

	/* Rotate: tiles[1]=old N-1 → N-2; tiles[0]=new frame → N-1 */
	memcpy(p->tiles[1], p->tiles[0], (size_t)p->tiles_n * 32u);
	memcpy(p->tiles[0], p->work_tiles, (size_t)p->tiles_n * 32u);
	tiles_to_rgb(p, p->tiles[0], p->rgb);
	p->frame_index++;
	return 1;
}
