/*
 * stvq_player.c - Stream decode STVQ into ping-pong back buffer.
 *
 * Each STFR payload is loaded with a single fread, then STPL/STCR/STVD/SND0
 * are parsed from memory (no per-replace disk I/O).
 */
#include "stvq_player.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *stvq_player_open_error;

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
	*id = stvq_read_be32(hdr);
	*size = stvq_read_be32(hdr + 4);
	return 0;
}

static int skip_bytes(FILE *fp, unsigned n)
{
	unsigned char buf[256];
	while (n) {
		unsigned chunk = n > sizeof(buf) ? (unsigned)sizeof(buf) : n;
		if (read_fully(fp, buf, chunk) != 0)
			return -1;
		n -= chunk;
	}
	return 0;
}

static int skip_pad(FILE *fp, uint32_t size)
{
	unsigned pad = stvq_iff_padded(size);
	if (pad > size)
		return skip_bytes(fp, pad - size);
	return 0;
}

static int load_stpl(StvqPlayer *p, uint32_t size, uint16_t out[16])
{
	unsigned char buf[32];
	int i;
	if (size != STVQ_STPL_BYTES || read_fully(p->fp, buf, STVQ_STPL_BYTES) != 0)
		return -1;
	for (i = 0; i < 16; i++)
		out[i] = stvq_read_be16(buf + i * 2);
	return skip_pad(p->fp, size);
}

static int load_stcb(StvqPlayer *p, uint32_t size)
{
	size_t need = (size_t)p->hdr.cb_entries * STVQ_TILE_BYTES;
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

static int ensure_frame_buf(StvqPlayer *p, size_t need)
{
	if (need <= p->frame_cap)
		return 0;
	{
		unsigned char *nbuf = (unsigned char *)realloc(p->frame_buf, need ? need : 1);
		if (!nbuf)
			return -1;
		p->frame_buf = nbuf;
		p->frame_cap = need;
	}
	return 0;
}

static int mem_parse_stpl(const unsigned char *body, uint32_t size, uint16_t out[16])
{
	int i;
	if (size != STVQ_STPL_BYTES)
		return -1;
	for (i = 0; i < 16; i++)
		out[i] = stvq_read_be16(body + i * 2);
	return 0;
}

static int mem_apply_stcr(StvqPlayer *p, const unsigned char *body, uint32_t size, unsigned long *out_n)
{
	unsigned n, i;
	if (size % STVQ_STCR_ENTRY_BYTES)
		return -1;
	n = size / STVQ_STCR_ENTRY_BYTES;
	for (i = 0; i < n; i++) {
		const unsigned char *ent = body + (size_t)i * STVQ_STCR_ENTRY_BYTES;
		uint16_t idx = stvq_read_be16(ent);
		if (idx >= p->hdr.cb_entries)
			return -1;
		memcpy(p->codebook + (size_t)idx * STVQ_TILE_BYTES, ent + 2, STVQ_TILE_BYTES);
	}
	if (out_n)
		*out_n = n;
	return 0;
}

/*
 * Decode STVD from memory into the back screen. Skip bits leave N-2 pixels.
 *
 * Trusted stream (release): no per-tile bounds / index checks.
 * Native BE loads: body is even-aligned (IFF pad + even STVD/SND0 sizes).
 * STVD layout is always even: 4*tiles_x + 2*indices.
 *
 * Dest walk: planar column addresses step +1,+7,+1,+7… (see stvq_tile_dest).
 */
static int mem_decode_stvd(StvqPlayer *p, const unsigned char *body, uint32_t size)
{
	const uint16_t *rp;
	unsigned col;
	unsigned tiles_x = p->tiles_x;
	unsigned tiles_y = p->tiles_y;
	uint8_t *back = stvq_hw_back(p->hw);
	unsigned ox = p->hw->origin_x;
	unsigned oy = p->hw->origin_y;
	const uint8_t *codebook = p->codebook;
	int need_full = (p->frame_index < 2);
	uint8_t *dst0;
#ifndef NDEBUG
	const unsigned char *end = body + size;
	unsigned cb_entries = p->hdr.cb_entries;
#else
	(void)size;
#endif

	/* Visible tile grid must fit (encoder pads); no per-tile bounds in the loop. */
	if (ox + tiles_x * 8u > STVQ_SCREEN_W || oy + tiles_y * 8u > STVQ_SCREEN_H)
		return -1;
	assert(((unsigned long)body & 1u) == 0u);

	rp = (const uint16_t *)(const void *)body;
	dst0 = stvq_tile_dest(back, ox, oy);

	assert(tiles_y <= 32u);

	for (col = 0; col < tiles_x; col++) {
		uint8_t *dst = dst0;
		uint32_t mask;
		unsigned row;

		mask = *(const uint32_t *)(const void *)rp;
		rp += 2;

		/* Walk bits MSB→LSB: add mask,mask ; bcs skip (X/C = former bit 31). */
		for (row = 0; row < tiles_y; row++, dst += 8u * STVQ_SCREEN_PITCH) {
			uint32_t sum = mask + mask;
			int skip = (sum < mask); /* carry out of bit 31 */
			mask = sum;
			if (skip) {
				if (need_full)
					return -1;
			} else {
				uint16_t idx = *rp++;
#ifndef NDEBUG
				assert((const unsigned char *)rp <= end);
				assert(idx < cb_entries);
#endif
				stvq_movep_tile(dst, codebook + ((unsigned)idx << 5));
			}
		}
		/* Next 8px column in ST planar layout. */
		dst0 += (col & 1u) ? 7u : 1u;
	}
#ifndef NDEBUG
	assert((const unsigned char *)rp <= end);
#endif
	return 0;
}

void stvq_player_close(StvqPlayer *p)
{
	if (p->fp) {
		fclose(p->fp);
		p->fp = NULL;
	}
	free(p->codebook);
	p->codebook = NULL;
	free(p->frame_buf);
	p->frame_buf = NULL;
	p->frame_cap = 0;
}

int stvq_player_open(StvqPlayer *p, StvqHw *hw, const char *path)
{
	uint32_t id, size;
	unsigned char raw[STVQ_STHD_SIZE];
	int have_sthd = 0, have_stpl = 0, have_stcb = 0;
	stvq_player_open_error = NULL;
	memset(p, 0, sizeof(*p));
	p->hw = hw;
	p->fp = fopen(path, "rb");
	if (!p->fp) {
		stvq_player_open_error = strerror(errno);
		return -1;
	}

	if (read_chunk_hdr(p->fp, &id, &size) != 0 || id != STVQ_CHUNK_FORM) {
		stvq_player_open_error = "not FORM";
		goto fail;
	}
	{
		unsigned char type[4];
		if (read_fully(p->fp, type, 4) != 0 || stvq_read_be32(type) != STVQ_CHUNK_STVQ) {
			stvq_player_open_error = "not STVQ";
			goto fail;
		}
	}

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
		} else if (id == STVQ_CHUNK_STPL) {
			if (load_stpl(p, size, p->initial_pal) != 0)
				goto fail;
			have_stpl = 1;
		} else if (id == STVQ_CHUNK_STCB) {
			if (!have_sthd)
				goto fail;
			if (load_stcb(p, size) != 0)
				goto fail;
			have_stcb = 1;
		} else if (id == STVQ_CHUNK_STFR) {
			if (fseek(p->fp, pos, SEEK_SET) != 0)
				goto fail;
			break;
		} else {
			if (skip_bytes(p->fp, stvq_iff_padded(size)) != 0)
				goto fail;
		}
	}

	if (!have_sthd || !have_stpl || !have_stcb) {
		stvq_player_open_error = "missing STHD/STPL/STCB";
		goto fail;
	}
	if (p->hdr.version != STVQ_VERSION) {
		stvq_player_open_error = "bad version";
		goto fail;
	}
	if (!p->hdr.fps)
		p->hdr.fps = 15;
	if (!p->hdr.sample_rate)
		p->hdr.sample_rate = (uint16_t)STVQ_SAMPLE_RATE;
	if (p->hdr.block_w != 8 || p->hdr.block_h != 8) {
		stvq_player_open_error = "bad block size";
		goto fail;
	}
	if (p->hdr.width > STVQ_SCREEN_W || p->hdr.height > STVQ_SCREEN_H) {
		stvq_player_open_error = "frame too large";
		goto fail;
	}

	/* Prefetch buffer using encoder hint when present. */
	if (p->hdr.max_frame_bytes) {
		if (ensure_frame_buf(p, stvq_iff_padded(p->hdr.max_frame_bytes)) != 0) {
			stvq_player_open_error = "oom frame buf";
			goto fail;
		}
	}

	p->frame_index = 0;
	return 0;

fail:
	stvq_player_close(p);
	return -1;
}

int stvq_player_next_frame(StvqPlayer *p, StvqFrame *out)
{
	uint32_t id, size;
	unsigned pad;
	unsigned consumed;
	int got_stvd = 0;
	unsigned long t0, t1, t_read0, t_read1;
	StvqProf *prof = p->prof;

	memset(out, 0, sizeof(*out));

	if (prof) {
		prof->last_read = 0;
		prof->last_stcr = 0;
		prof->last_decode = 0;
		prof->last_audio = 0;
		prof->last_stfr_bytes = 0;
		prof->last_stcr_n = 0;
		prof->last_pcm_bytes = 0;
		prof->single_read = 1;
	}

	if (p->eof)
		return 0;

	t0 = stvq_hz200();

	if (read_chunk_hdr(p->fp, &id, &size) != 0)
		return -1;
	if (id == STVQ_CHUNK_STEN) {
		p->eof = 1;
		return 0;
	}
	if (id != STVQ_CHUNK_STFR)
		return -1;

	pad = stvq_iff_padded(size);
	if (ensure_frame_buf(p, pad ? pad : 1) != 0)
		return -1;

	/* ---- one-shot STFR payload read ---- */
	t_read0 = stvq_hz200();
	if (pad && read_fully(p->fp, p->frame_buf, pad) != 0)
		return -1;
	t_read1 = stvq_hz200();
	if (prof) {
		prof->last_read = t_read1 - t_read0;
		prof->last_stfr_bytes = size;
	}

	consumed = 0;
	while (consumed + 8u <= size) {
		uint32_t cid = stvq_read_be32(p->frame_buf + consumed);
		uint32_t csize = stvq_read_be32(p->frame_buf + consumed + 4);
		unsigned cpad = stvq_iff_padded(csize);
		const unsigned char *cbody = p->frame_buf + consumed + 8;

		if (consumed + 8u + cpad > pad)
			return -1;
		consumed += 8u + cpad;

		if (cid == STVQ_CHUNK_STPL) {
			if (mem_parse_stpl(cbody, csize, out->stpl) != 0)
				return -1;
			out->have_stpl = 1;
		} else if (cid == STVQ_CHUNK_STCR) {
			unsigned long n = 0;
			unsigned long ts = stvq_hz200();
			if (mem_apply_stcr(p, cbody, csize, &n) != 0)
				return -1;
			if (prof) {
				prof->last_stcr += stvq_hz200() - ts;
				prof->last_stcr_n = n;
			}
		} else if (cid == STVQ_CHUNK_STVD) {
			unsigned long ts = stvq_hz200();
			if (mem_decode_stvd(p, cbody, csize) != 0)
				return -1;
			if (prof)
				prof->last_decode += stvq_hz200() - ts;
			got_stvd = 1;
		} else if (cid == STVQ_CHUNK_SND0) {
			out->pcm = cbody;
			out->pcm_len = csize;
			if (prof)
				prof->last_pcm_bytes = csize;
		}
		/* else: unknown nested chunk — already skipped via consumed */
	}

	if (!got_stvd)
		return -1;

	t1 = stvq_hz200();
	(void)t0;
	(void)t1;

	p->frame_index++;
	return 1;
}
