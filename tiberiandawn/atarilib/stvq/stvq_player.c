/*
 * stvq_player.c - Stream decode STVQ into ping-pong back buffer.
 *
 * After open / each successful next_frame, IO sits after the next top-level
 * chunk header and next_size holds its size (0 = finished). next_frame reads
 * payload + following header in one IO read, then parses in memory.
 *
 * Chunk sizes on disk are exact (encoder keeps them even); no IFF pad math.
 */
#include "stvq_player.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *stvq_player_open_error;

static int io_read_fully(const StvqIo *io, void *buf, size_t n)
{
	unsigned char *p = (unsigned char *)buf;
	size_t got = 0;
	while (got < n) {
		size_t r = io->read(io->user, p + got, n - got);
		if (r == 0)
			return -1;
		got += r;
	}
	return 0;
}

static int read_chunk_hdr(const StvqIo *io, uint32_t *id, uint32_t *size)
{
	unsigned char hdr[8] = {0};
	if (io_read_fully(io, hdr, 8) != 0)
		return -1;
	*id = stvq_read_be32(hdr);
	*size = stvq_read_be32(hdr + 4);
	return 0;
}

static int skip_bytes(const StvqIo *io, unsigned n)
{
	unsigned char buf[256] = {0};
	while (n) {
		unsigned chunk = n > sizeof(buf) ? (unsigned)sizeof(buf) : n;
		if (io_read_fully(io, buf, chunk) != 0)
			return -1;
		n -= chunk;
	}
	return 0;
}

static int load_stpl(StvqPlayer *p, uint32_t size, uint16_t out[16])
{
	unsigned char buf[32] = {0};
	if (size != STVQ_STPL_BYTES || io_read_fully(p->io, buf, STVQ_STPL_BYTES) != 0)
		return -1;
	for (int i = 0; i < 16; i++)
		out[i] = stvq_read_be16(buf + i * 2);
	return 0;
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
	if (io_read_fully(p->io, p->codebook, need) != 0)
		return -1;
	return 0;
}

static int ensure_frame_buf(StvqPlayer *p, size_t need)
{
	if (need <= p->frame_cap)
		return 0;
	unsigned char *nbuf = (unsigned char *)realloc(p->frame_buf, need ? need : 1);
	if (!nbuf)
		return -1;
	p->frame_buf = nbuf;
	p->frame_cap = need;
	return 0;
}

static int mem_parse_stpl(const unsigned char *body, uint32_t size, uint16_t out[16])
{
	if (size != STVQ_STPL_BYTES)
		return -1;
	for (int i = 0; i < 16; i++)
		out[i] = stvq_read_be16(body + i * 2);
	return 0;
}

static int mem_apply_stcr(StvqPlayer *p, const unsigned char *body, uint32_t size, unsigned long *out_n)
{
	const unsigned char *rp = body;
	const unsigned char *end;
	unsigned long n = 0;

	if (size < STVQ_STCR_ENTRY_BYTES) {
		if (out_n)
			*out_n = 0;
		return 0;
	}
	/* Last valid entry start: compare without per-iter (rp + 34 <= end). */
	end = body + size - STVQ_STCR_ENTRY_BYTES;
	while (rp <= end) {
		uint16_t idx = stvq_read_be16(rp);
		uint32_t *d;
		const uint32_t *s;
		unsigned i;
		assert(idx < p->hdr.cb_entries);
		/* 32-byte tile: mint GCC does not inline memcpy(,,32). */
		d = (uint32_t *)(void *)(p->codebook + ((unsigned)idx << 5));
		s = (const uint32_t *)(const void *)(rp + 2);
		#pragma GCC unroll 8
		for (i = 0; i < STVQ_TILE_BYTES / 4u; i++)
			d[i] = s[i];
		rp += STVQ_STCR_ENTRY_BYTES;
		n++;
	}
	if (out_n)
		*out_n = n;
	return 0;
}

/*
 * Decode STVD from memory into the back screen. Skip bits leave N-2 pixels.
 *
 * Trusted stream (release): no per-tile bounds / index checks.
 * Native BE loads: body is even-aligned (even STVD/SND0 sizes).
 * STVD layout is always even: 4*tiles_x + 2*indices.
 *
 * Dest walk: planar column addresses step +1,+7,+1,+7... (see stvq_tile_dest).
 */
static int mem_decode_stvd(StvqPlayer *p, const unsigned char *body, uint32_t size)
{
	uint16_t tiles_x = p->tiles_x;
	uint16_t tiles_y = p->tiles_y;
	uint8_t *back = stvq_hw_back(p->hw);
	unsigned ox = p->hw->origin_x;
	unsigned oy = p->hw->origin_y;
	const uint8_t *codebook = p->codebook;
#ifndef NDEBUG
	const unsigned char *end = body + size;
#else
	(void)size;
#endif

	/* Visible tile grid must fit (encoder pads); no per-tile bounds in the loop. */
	if (ox + (unsigned)tiles_x * 8u > STVQ_SCREEN_W || oy + (unsigned)tiles_y * 8u > STVQ_SCREEN_H)
		return -1;
	assert(((unsigned long)body & 1u) == 0u);
	assert(tiles_y <= 32u);

	const uint16_t *rp = (const uint16_t *)(const void *)body;
	uint8_t *dst0 = stvq_tile_dest(back, ox, oy);

	for (uint16_t col = 0; col < tiles_x; col++) {
		uint8_t *dst = dst0;
		uint32_t mask = *(const uint32_t *)(const void *)rp;
		uint16_t *ip = (uint16_t *)(unsigned long)(const void *)(rp + 2);

		/* Walk bits MSB->LSB: add mask,mask ; bcs skip (X/C = former bit 31). */
		for (uint16_t row = 0; row < tiles_y; row++, dst += 8u * STVQ_SCREEN_PITCH) {
#if defined(__GNUC__) && (defined(__mc68000__) || defined(__mc68020__) || defined(__M68000__) || defined(__m68k__))
			__asm__ __volatile__(
				"add.l %[mask],%[mask]\n\t"
				"bcs.s 1f\n\t"
				"moveq #0,%%d2\n\t"
				"move.w (%[ip])+,%%d2\n\t"
				"lsl.l #5,%%d2\n\t"
				"move.l %[cb],%%a1\n\t"
				"add.l %%d2,%%a1\n\t"
				"move.l (%%a1)+,%%d2\n\t"
				"movep.l %%d2,0(%[dst])\n\t"
				"move.l (%%a1)+,%%d2\n\t"
				"movep.l %%d2,160(%[dst])\n\t"
				"move.l (%%a1)+,%%d2\n\t"
				"movep.l %%d2,320(%[dst])\n\t"
				"move.l (%%a1)+,%%d2\n\t"
				"movep.l %%d2,480(%[dst])\n\t"
				"move.l (%%a1)+,%%d2\n\t"
				"movep.l %%d2,640(%[dst])\n\t"
				"move.l (%%a1)+,%%d2\n\t"
				"movep.l %%d2,800(%[dst])\n\t"
				"move.l (%%a1)+,%%d2\n\t"
				"movep.l %%d2,960(%[dst])\n\t"
				"move.l (%%a1)+,%%d2\n\t"
				"movep.l %%d2,1120(%[dst])\n"
				"1:"
				: [mask] "+d"(mask), [ip] "+a"(ip)
				: [dst] "a"(dst), [cb] "a"(codebook)
				: "d2", "a1", "cc", "memory");
#else
			uint32_t sum = mask + mask;
			int skip = (sum < mask);
			mask = sum;
			if (!skip) {
				uint16_t idx = *ip++;
#ifndef NDEBUG
				assert((const unsigned char *)ip <= end);
				assert(idx < p->hdr.cb_entries);
#endif
				stvq_movep_tile(dst, codebook + ((unsigned)idx << 5));
			}
#endif
		}
		rp = ip;
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
	p->io = NULL;
	free(p->codebook);
	p->codebook = NULL;
	free(p->frame_buf);
	p->frame_buf = NULL;
	p->frame_cap = 0;
	p->next_size = 0;
}

int stvq_player_open(StvqPlayer *p, StvqHw *hw, const StvqIo *io)
{
	uint32_t id = 0, size = 0;
	unsigned char raw[STVQ_STHD_SIZE] = {0};
	int have_sthd = 0, have_stpl = 0;
	int have_next = 0;
	stvq_player_open_error = NULL;
	memset(p, 0, sizeof(*p));
	p->hw = hw;
	p->io = io;
	if (!io || !io->read) {
		stvq_player_open_error = "bad io";
		return -1;
	}

	if (read_chunk_hdr(io, &id, &size) != 0 || id != STVQ_CHUNK_FORM) {
		stvq_player_open_error = "not FORM";
		goto fail;
	}
	unsigned char type[4] = {0};
	if (io_read_fully(io, type, 4) != 0 || stvq_read_be32(type) != STVQ_CHUNK_STVQ) {
		stvq_player_open_error = "not STVQ";
		goto fail;
	}

	while (!have_next) {
		if (read_chunk_hdr(io, &id, &size) != 0)
			goto fail;
		if (id == STVQ_CHUNK_STHD) {
			if (size != STVQ_STHD_SIZE || io_read_fully(io, raw, STVQ_STHD_SIZE) != 0)
				goto fail;
			stvq_header_unpack(raw, &p->hdr);
			have_sthd = 1;
			p->tiles_x = stvq_tiles_x(p->hdr.width);
			p->tiles_y = stvq_tiles_y(p->hdr.height);
		} else if (id == STVQ_CHUNK_STPL) {
			if (load_stpl(p, size, p->initial_pal) != 0)
				goto fail;
			have_stpl = 1;
		} else if (id == STVQ_CHUNK_STCB) {
			/* Legacy optional full codebook */
			if (!have_sthd)
				goto fail;
			if (load_stcb(p, size) != 0)
				goto fail;
		} else if (id == STVQ_CHUNK_STFR || id == STVQ_CHUNK_STEN) {
			/* Header consumed; IO after it — establish prefetch invariant. */
			if (!have_sthd || !have_stpl) {
				stvq_player_open_error = "missing STHD/STPL";
				goto fail;
			}
			p->next_size = size;
			have_next = 1;
		} else {
			if (skip_bytes(io, size) != 0)
				goto fail;
		}
	}

	if (!p->codebook) {
		size_t need = (size_t)p->hdr.cb_entries * STVQ_TILE_BYTES;
		p->codebook = (uint8_t *)calloc(1, need ? need : 1);
		if (!p->codebook) {
			stvq_player_open_error = "oom codebook";
			goto fail;
		}
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

	/* Prefetch buffer using encoder hint when present (+8 for next header). */
	if (p->hdr.max_frame_bytes) {
		if (ensure_frame_buf(p, (size_t)p->hdr.max_frame_bytes + 8u) != 0) {
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

int stvq_player_read_frame(StvqPlayer *p)
{
	StvqProf *prof = p->prof;
	uint32_t size;
	size_t need;
	size_t got;
	unsigned long t_read0;
	unsigned long t_read1;

	if (prof) {
		prof->last_read = 0;
		prof->last_stfr_bytes = 0;
	}

	if (p->load_ready)
		return 1;
	if (p->next_size == 0)
		return 0;

	size = p->next_size;
	need = (size_t)size + 8u;
	if (ensure_frame_buf(p, need) != 0)
		return -1;

	t_read0 = stvq_hz200();
	got = p->io->read(p->io->user, p->frame_buf, need);
	t_read1 = stvq_hz200();
	if (got != need && got != (size_t)size)
		return -1;
	if (prof) {
		prof->last_read = t_read1 - t_read0;
		prof->last_stfr_bytes = size;
	}

	p->load_size = size;
	p->load_got = got;
	p->load_ready = 1;
	return 1;
}

int stvq_player_decode_frame(StvqPlayer *p, StvqFrame *out)
{
	int got_stvd = 0;
	StvqProf *prof = p->prof;
	uint32_t size;
	size_t got;
	const unsigned char *rp;
	const unsigned char *end;

	memset(out, 0, sizeof(*out));

	if (prof) {
		prof->last_stcr = 0;
		prof->last_decode = 0;
		prof->last_audio = 0;
		prof->last_stcr_n = 0;
		prof->last_pcm_bytes = 0;
	}

	if (!p->load_ready)
		return -1;

	size = p->load_size;
	got = p->load_got;
	rp = p->frame_buf;
	end = p->frame_buf + size;
	while (rp + 8 <= end) {
		uint32_t cid = stvq_read_be32(rp);
		uint32_t csize = stvq_read_be32(rp + 4);
		rp += 8;
		if (rp + csize > end)
			return -1;

		if (cid == STVQ_CHUNK_STPL) {
			if (mem_parse_stpl(rp, csize, out->stpl) != 0)
				return -1;
			out->have_stpl = 1;
		} else if (cid == STVQ_CHUNK_STCR) {
			unsigned long n = 0;
			unsigned long ts = stvq_hz200();
			if (mem_apply_stcr(p, rp, csize, &n) != 0)
				return -1;
			if (prof) {
				prof->last_stcr += stvq_hz200() - ts;
				prof->last_stcr_n = n;
			}
		} else if (cid == STVQ_CHUNK_STVD) {
			unsigned long ts = stvq_hz200();
			if (mem_decode_stvd(p, rp, csize) != 0)
				return -1;
			if (prof)
				prof->last_decode += stvq_hz200() - ts;
			got_stvd = 1;
		} else if (cid == STVQ_CHUNK_SND0) {
			out->pcm = rp;
			out->pcm_len = csize;
			if (prof)
				prof->last_pcm_bytes = csize;
		}
		rp += csize;
	}

	if (!got_stvd)
		return -1;

	if (got == (size_t)size + 8u)
		p->next_size = stvq_read_be32(p->frame_buf + size + 4);
	else
		p->next_size = 0;

	p->load_ready = 0;
	p->frame_index++;
	return 1;
}

int stvq_player_next_frame(StvqPlayer *p, StvqFrame *out)
{
	int pr = stvq_player_read_frame(p);
	if (pr != 1)
		return pr;
	return stvq_player_decode_frame(p, out);
}
