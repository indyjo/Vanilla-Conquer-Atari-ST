/*
 * vqa_decode.c - Sequential host decode of Westwood VQA (4x2 TD path).
 */
#include "vqa_decode.h"

#include "vqa_io.h"

#ifdef __cplusplus
extern "C" {
#endif
int vqa_lcw_uncompress(void const *source, void *dest, unsigned length);
void vqa_unvq_4x2(uint8_t *codebook, uint8_t *pointers, uint8_t *buffer, unsigned blocks_per_row,
    unsigned num_rows, unsigned buff_width);
#ifdef __cplusplus
}
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t fnv1a(const unsigned char *data, size_t len)
{
	uint32_t h = 2166136261u;
	size_t i;
	for (i = 0; i < len; i++) {
		h ^= data[i];
		h *= 16777619u;
	}
	return h;
}

/* Standard IMA ADPCM (SOS/SND2 style): 4-bit -> 16-bit PCM. */
static const int ima_index_table[16] = {
	-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8
};
static const int ima_step_table[89] = {
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
	73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
	494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499,
	2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
	12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

typedef struct ImaState {
	int predictor;
	int step_index;
} ImaState;

static int16_t ima_decode_nibble(ImaState *st, int nibble)
{
	int step = ima_step_table[st->step_index];
	int diff = step >> 3;
	if (nibble & 1)
		diff += step >> 2;
	if (nibble & 2)
		diff += step >> 1;
	if (nibble & 4)
		diff += step;
	if (nibble & 8)
		st->predictor -= diff;
	else
		st->predictor += diff;
	if (st->predictor > 32767)
		st->predictor = 32767;
	if (st->predictor < -32768)
		st->predictor = -32768;
	st->step_index += ima_index_table[nibble];
	if (st->step_index < 0)
		st->step_index = 0;
	if (st->step_index > 88)
		st->step_index = 88;
	return (int16_t)st->predictor;
}

static int ima_decode_block(ImaState *st, const unsigned char *src, size_t src_len, int16_t *dst, size_t *out_n)
{
	size_t i, n = 0;
	for (i = 0; i < src_len; i++) {
		dst[n++] = ima_decode_nibble(st, src[i] & 0x0f);
		dst[n++] = ima_decode_nibble(st, (src[i] >> 4) & 0x0f);
	}
	*out_n = n;
	return 0;
}

typedef struct DecodeCtx {
	VqaReader r;
	VqaHeader hdr;
	unsigned char *cb_a;
	unsigned char *cb_b;
	unsigned char *cur_cb;  /* codebook used by UnVQ this frame (FullCB) */
	unsigned char *next_cb; /* partial CBP accumulation (CurCB) */
	unsigned max_cb;
	unsigned partial_size;
	unsigned num_partial;
	unsigned cbpz_off;
	int cb_new_ready; /* next_cb holds a completed book; promote next frame */
	unsigned char *vpt;
	unsigned max_vpt;
	unsigned char pal[VQA_PALETTE_BYTES];
	int have_pal;
	ImaState ima;
	int16_t *pcm_accum;
	size_t pcm_count;
	size_t pcm_cap;
} DecodeCtx;

/* Westwood: Codebook = FullCB at frame start; CBP completion updates FullCB for the *next* frame. */
static void promote_pending_codebook(DecodeCtx *d)
{
	if (!d->cb_new_ready)
		return;
	{
		unsigned char *tmp = d->cur_cb;
		d->cur_cb = d->next_cb;
		d->next_cb = tmp;
	}
	d->cb_new_ready = 0;
}

static int pcm_append(DecodeCtx *d, const int16_t *s, size_t n)
{
	if (d->pcm_count + n > d->pcm_cap) {
		size_t ncap = d->pcm_cap ? d->pcm_cap * 2u : 65536u;
		while (ncap < d->pcm_count + n)
			ncap *= 2u;
		int16_t *p = (int16_t *)realloc(d->pcm_accum, ncap * sizeof(int16_t));
		if (!p)
			return -1;
		d->pcm_accum = p;
		d->pcm_cap = ncap;
	}
	memcpy(d->pcm_accum + d->pcm_count, s, n * sizeof(int16_t));
	d->pcm_count += n;
	return 0;
}

static int read_chunk(VqaReader *r, uint32_t *id, uint32_t *size)
{
	return vqa_reader_read_chunk_hdr(r, id, size);
}

static int load_cbf0(DecodeCtx *d, uint32_t size)
{
	unsigned pad = vqa_iff_data_padded(size);
	if (pad > d->max_cb)
		return -1;
	/* CBF0 fills the active codebook in place (same buffer Codebook already uses). */
	if (vqa_reader_read(&d->r, d->cur_cb, pad) != 0)
		return -1;
	d->partial_size = 0;
	d->num_partial = 0;
	d->cbpz_off = 0;
	d->cb_new_ready = 0;
	return 0;
}

static int load_cbfz(DecodeCtx *d, uint32_t size)
{
	unsigned pad = vqa_iff_data_padded(size);
	unsigned char *comp;
	if (pad > d->max_cb)
		return -1;
	comp = (unsigned char *)malloc(pad ? pad : 1);
	if (!comp)
		return -1;
	if (vqa_reader_read(&d->r, comp, pad) != 0) {
		free(comp);
		return -1;
	}
	memset(d->cur_cb, 0, d->max_cb);
	if (vqa_lcw_uncompress(comp, d->cur_cb, d->max_cb) <= 0) {
		free(comp);
		return -1;
	}
	free(comp);
	d->partial_size = 0;
	d->num_partial = 0;
	d->cbpz_off = 0;
	d->cb_new_ready = 0;
	return 0;
}

static int load_cbp0(DecodeCtx *d, uint32_t size)
{
	unsigned pad = vqa_iff_data_padded(size);
	unsigned group = d->hdr.groupsize ? d->hdr.groupsize : 8;
	if (d->partial_size + pad > d->max_cb)
		return -1;
	if (vqa_reader_read(&d->r, d->next_cb + d->partial_size, pad) != 0)
		return -1;
	d->partial_size += size;
	d->num_partial++;
	if (d->num_partial == group) {
		/* New FullCB is ready, but UnVQ for *this* frame still uses cur_cb. */
		d->partial_size = 0;
		d->num_partial = 0;
		d->cb_new_ready = 1;
	}
	return 0;
}

static int load_cbpz(DecodeCtx *d, uint32_t size)
{
	unsigned pad = vqa_iff_data_padded(size);
	unsigned group = d->hdr.groupsize ? d->hdr.groupsize : 8;
	unsigned char *dst;
	if (d->num_partial == 0) {
		/* Compressed partials accumulate in the high half (like Westwood CBOffset). */
		d->cbpz_off = d->max_cb / 2u;
		d->partial_size = 0;
	}
	/*
	 * Match VQA_Load_CBPZ: read IFF-padded bytes, but advance PartialCBSize by
	 * iffsize only so the next chunk overwrites the pad byte. Leaving pads in
	 * the stream corrupts LCW (breaks every groupsize frames).
	 */
	if (d->cbpz_off + d->partial_size + pad > d->max_cb)
		return -1;
	dst = d->next_cb + d->cbpz_off + d->partial_size;
	if (vqa_reader_read(&d->r, dst, pad) != 0)
		return -1;
	d->partial_size += size;
	d->num_partial++;
	if (d->num_partial == group) {
		unsigned char *comp = d->next_cb + d->cbpz_off;
		unsigned comp_len = d->partial_size;
		unsigned char *tmpbuf = (unsigned char *)malloc(comp_len ? comp_len : 1);
		unsigned char *decomp_dst;
		if (!tmpbuf)
			return -1;
		memcpy(tmpbuf, comp, comp_len);
		decomp_dst = (unsigned char *)malloc(d->max_cb);
		if (!decomp_dst) {
			free(tmpbuf);
			return -1;
		}
		memset(decomp_dst, 0, d->max_cb);
		if (vqa_lcw_uncompress(tmpbuf, decomp_dst, d->max_cb) <= 0) {
			free(tmpbuf);
			free(decomp_dst);
			return -1;
		}
		free(tmpbuf);
		memcpy(d->next_cb, decomp_dst, d->max_cb);
		free(decomp_dst);
		d->partial_size = 0;
		d->num_partial = 0;
		d->cbpz_off = 0;
		d->cb_new_ready = 1;
	}
	return 0;
}

static int load_cpl0(DecodeCtx *d, uint32_t size)
{
	unsigned want = size < VQA_PALETTE_BYTES ? size : VQA_PALETTE_BYTES;
	unsigned pad = vqa_iff_data_padded(size);
	memset(d->pal, 0, VQA_PALETTE_BYTES);
	if (vqa_reader_read(&d->r, d->pal, want) != 0)
		return -1;
	if (pad > want && vqa_reader_skip(&d->r, pad - want) != 0)
		return -1;
	/* Match game Set_Palette path: guns are 6-bit (& 63). */
	vqa_sanitize_vga6_palette(d->pal);
	d->have_pal = 1;
	return 0;
}

static int load_vptz(DecodeCtx *d, uint32_t size)
{
	unsigned pad = vqa_iff_data_padded(size);
	unsigned char *comp;
	if (pad > d->max_vpt)
		return -1;
	comp = (unsigned char *)malloc(pad ? pad : 1);
	if (!comp)
		return -1;
	if (vqa_reader_read(&d->r, comp, pad) != 0) {
		free(comp);
		return -1;
	}
	memset(d->vpt, 0, d->max_vpt);
	if (vqa_lcw_uncompress(comp, d->vpt, d->max_vpt) <= 0) {
		free(comp);
		return -1;
	}
	free(comp);
	return 0;
}

static int load_vpt0(DecodeCtx *d, uint32_t size)
{
	unsigned pad = vqa_iff_data_padded(size);
	if (pad > d->max_vpt)
		return -1;
	memset(d->vpt, 0, d->max_vpt);
	if (vqa_reader_read(&d->r, d->vpt, pad) != 0)
		return -1;
	return 0;
}

static int load_snd(DecodeCtx *d, uint32_t id, uint32_t size)
{
	unsigned pad = vqa_iff_data_padded(size);
	unsigned char *buf = (unsigned char *)malloc(pad ? pad : 1);
	int16_t *pcm;
	size_t n = 0;
	if (!buf)
		return -1;
	if (vqa_reader_read(&d->r, buf, pad) != 0) {
		free(buf);
		return -1;
	}
	if (id == VQA_CHUNK_SND0) {
		/* 16-bit PCM LE typically */
		size_t samples = size / 2u;
		size_t i;
		pcm = (int16_t *)malloc(samples * sizeof(int16_t));
		if (!pcm) {
			free(buf);
			return -1;
		}
		for (i = 0; i < samples; i++) {
			pcm[i] = (int16_t)(buf[i * 2] | (buf[i * 2 + 1] << 8));
		}
		if (pcm_append(d, pcm, samples) != 0) {
			free(pcm);
			free(buf);
			return -1;
		}
		free(pcm);
	} else if (id == VQA_CHUNK_SND2 || id == VQA_CHUNK_SND1) {
		pcm = (int16_t *)malloc((size * 2u + 8u) * sizeof(int16_t));
		if (!pcm) {
			free(buf);
			return -1;
		}
		ima_decode_block(&d->ima, buf, size, pcm, &n);
		if (pcm_append(d, pcm, n) != 0) {
			free(pcm);
			free(buf);
			return -1;
		}
		free(pcm);
	} else {
		/* skip unknown audio */
	}
	free(buf);
	return 0;
}

static int handle_frame_body(DecodeCtx *d, uint32_t body_size, int *got_vpt, int *got_pal)
{
	unsigned consumed = 0;
	*got_vpt = 0;
	*got_pal = 0;
	while (consumed < body_size) {
		uint32_t id, size;
		unsigned pad;
		if (read_chunk(&d->r, &id, &size) != 0)
			return -1;
		pad = vqa_iff_data_padded(size);
		consumed += 8u + pad;
		if (id == VQA_CHUNK_CPL0) {
			if (load_cpl0(d, size) != 0)
				return -1;
			*got_pal = 1;
		} else if (id == VQA_CHUNK_CBF0) {
			if (load_cbf0(d, size) != 0)
				return -1;
		} else if (id == VQA_CHUNK_CBFZ) {
			if (load_cbfz(d, size) != 0)
				return -1;
		} else if (id == VQA_CHUNK_CBP0) {
			if (load_cbp0(d, size) != 0)
				return -1;
		} else if (id == VQA_CHUNK_CBPZ) {
			if (load_cbpz(d, size) != 0)
				return -1;
		} else if (id == VQA_CHUNK_VPTZ || id == VQA_CHUNK_VPRZ || id == VQA_CHUNK_VPTD ||
		           id == VQA_CHUNK_VPTK || id == VQA_CHUNK_VPDZ || id == VQA_CHUNK_VPKZ) {
			if (load_vptz(d, size) != 0)
				return -1;
			*got_vpt = 1;
		} else if (id == VQA_CHUNK_VPT0 || id == VQA_CHUNK_VPTR) {
			if (load_vpt0(d, size) != 0)
				return -1;
			*got_vpt = 1;
		} else if (id == VQA_CHUNK_SND0 || id == VQA_CHUNK_SND1 || id == VQA_CHUNK_SND2) {
			if (load_snd(d, id, size) != 0)
				return -1;
		} else {
			if (vqa_reader_skip(&d->r, pad) != 0)
				return -1;
		}
	}
	return 0;
}

void vqa_decode_free(VqaDecode *dec)
{
	unsigned i;
	if (!dec)
		return;
	if (dec->frames) {
		for (i = 0; i < dec->frame_count; i++) {
			free(dec->frames[i].pixels);
			free(dec->frames[i].pcm16);
		}
		free(dec->frames);
	}
	free(dec->segments);
	free(dec->all_pcm16);
	memset(dec, 0, sizeof(*dec));
}

int vqa_decode_file(const char *path, VqaDecode *out)
{
	DecodeCtx d;
	uint32_t id, size;
	unsigned fi = 0;
	unsigned seg_cap = 0;
	int prev_hash_valid = 0;
	uint32_t prev_hash = 0;
	size_t pcm_cursor = 0;

	memset(out, 0, sizeof(*out));
	memset(&d, 0, sizeof(d));

	if (vqa_reader_open(&d.r, path) != 0)
		return -1;

	if (read_chunk(&d.r, &id, &size) != 0 || id != VQA_CHUNK_FORM)
		goto fail;
	if (vqa_reader_read(&d.r, &id, 4) != 0 || id != VQA_CHUNK_WVQA)
		goto fail;

	/* scan until FINF / collect header */
	for (;;) {
		off_t pos = vqa_reader_tell(&d.r);
		if (pos >= d.r.size)
			break;
		if (read_chunk(&d.r, &id, &size) != 0)
			goto fail;
		if (id == VQA_CHUNK_VQHD) {
			unsigned char raw[VQA_VQHD_SIZE];
			if (size != VQA_VQHD_SIZE || vqa_reader_read(&d.r, raw, sizeof(raw)) != 0)
				goto fail;
			d.hdr.version = vqa_read_le16(raw + 0);
			d.hdr.flags = vqa_read_le16(raw + 2);
			d.hdr.frames = vqa_read_le16(raw + 4);
			d.hdr.image_width = vqa_read_le16(raw + 6);
			d.hdr.image_height = vqa_read_le16(raw + 8);
			d.hdr.block_width = raw[10];
			d.hdr.block_height = raw[11];
			d.hdr.fps = raw[12];
			d.hdr.groupsize = raw[13] ? raw[13] : 8;
			d.hdr.num1_colors = vqa_read_le16(raw + 14);
			d.hdr.cb_entries = vqa_read_le16(raw + 16);
			d.hdr.sample_rate = vqa_read_le16(raw + 24);
			d.hdr.channels = raw[26];
			d.hdr.bits_per_sample = raw[27];
			break;
		}
		if (vqa_reader_skip(&d.r, vqa_iff_data_padded(size)) != 0)
			goto fail;
	}

	if (!d.hdr.frames || !d.hdr.block_width || !d.hdr.block_height)
		goto fail;

	out->hdr = d.hdr;
	out->width = d.hdr.image_width;
	out->height = d.hdr.image_height;
	out->blocks_w = out->width / d.hdr.block_width;
	out->blocks_h = out->height / d.hdr.block_height;
	out->sample_rate = d.hdr.sample_rate;
	out->channels = d.hdr.channels ? d.hdr.channels : 1;
	out->bits_per_sample = d.hdr.bits_per_sample ? d.hdr.bits_per_sample : 16;
	out->frame_count = d.hdr.frames;

	d.max_cb = (unsigned)d.hdr.cb_entries * 8u + 4096u;
	if (d.max_cb < 32768u)
		d.max_cb = 32768u;
	d.max_vpt = out->blocks_w * out->blocks_h * 2u + 4096u;
	d.cb_a = (unsigned char *)calloc(1, d.max_cb);
	d.cb_b = (unsigned char *)calloc(1, d.max_cb);
	d.vpt = (unsigned char *)calloc(1, d.max_vpt);
	d.cur_cb = d.cb_a;
	d.next_cb = d.cb_b;
	out->frames = (VqaDecodedFrame *)calloc(out->frame_count, sizeof(VqaDecodedFrame));
	if (!d.cb_a || !d.cb_b || !d.vpt || !out->frames)
		goto fail;

	/* Rewind to after WVQA and process all chunks */
	if (vqa_reader_seek(&d.r, 12) != 0)
		goto fail;

	while (vqa_reader_tell(&d.r) + 8 <= d.r.size && fi < out->frame_count) {
		if (read_chunk(&d.r, &id, &size) != 0)
			goto fail;
		if (id == VQA_CHUNK_VQFR || id == VQA_CHUNK_VQFL || id == VQA_CHUNK_VQFK) {
			int got_vpt = 0, got_pal = 0;
			VqaDecodedFrame *fr = &out->frames[fi];
			size_t pix = (size_t)out->width * out->height;
			fr->index = (int)fi;
			fr->pixels = (unsigned char *)calloc(1, pix);
			if (!fr->pixels)
				goto fail;
			/* Match Westwood: bind FullCB before reading this frame's chunks. */
			promote_pending_codebook(&d);
			if (handle_frame_body(&d, size, &got_vpt, &got_pal) != 0)
				goto fail;
			if (!got_vpt) {
				fprintf(stderr, "error: frame %u missing VPT\n", fi);
				goto fail;
			}
			vqa_unvq_4x2(d.cur_cb, d.vpt, fr->pixels, out->blocks_w, out->blocks_h, out->width);
			if (got_pal && d.have_pal) {
				uint32_t hash = fnv1a(d.pal, VQA_PALETTE_BYTES);
				fr->has_palette_change = 1;
				if (!prev_hash_valid || hash != prev_hash) {
					if (out->segment_count == seg_cap) {
						unsigned ncap = seg_cap ? seg_cap * 2u : 8u;
						VqaPalSegment *ns =
						    (VqaPalSegment *)realloc(out->segments, ncap * sizeof(*ns));
						if (!ns)
							goto fail;
						out->segments = ns;
						seg_cap = ncap;
					}
					{
						VqaPalSegment *s = &out->segments[out->segment_count];
						memset(s, 0, sizeof(*s));
						s->start_frame = (int)fi;
						s->end_frame = (int)fi;
						memcpy(s->pal, d.pal, VQA_PALETTE_BYTES);
						s->hash = hash;
						out->segment_count++;
					}
					prev_hash = hash;
					prev_hash_valid = 1;
				}
			}
			if (out->segment_count) {
				out->segments[out->segment_count - 1].end_frame = (int)fi;
				fr->segment = (int)out->segment_count - 1;
			} else {
				fr->segment = 0;
			}
			fi++;
		} else if (id == VQA_CHUNK_SND0 || id == VQA_CHUNK_SND1 || id == VQA_CHUNK_SND2 ||
		           id == VQA_CHUNK_SNA0 || id == VQA_CHUNK_SNA1 || id == VQA_CHUNK_SNA2) {
			if (load_snd(&d, id, size) != 0)
				goto fail;
		} else {
			if (vqa_reader_skip(&d.r, vqa_iff_data_padded(size)) != 0)
				goto fail;
		}
	}

	if (fi != out->frame_count) {
		fprintf(stderr, "error: decoded %u frames, header says %u\n", fi, out->frame_count);
		goto fail;
	}

	/* If no palette chunk seen oddly, still need a segment — use black */
	if (out->segment_count == 0) {
		out->segments = (VqaPalSegment *)calloc(1, sizeof(VqaPalSegment));
		if (!out->segments)
			goto fail;
		out->segment_count = 1;
		out->segments[0].start_frame = 0;
		out->segments[0].end_frame = (int)out->frame_count - 1;
	}

	/* Distribute PCM across frames by rate/fps */
	out->all_pcm16 = d.pcm_accum;
	out->all_pcm16_count = d.pcm_count;
	d.pcm_accum = NULL;
	if (out->all_pcm16_count && out->hdr.fps) {
		for (fi = 0; fi < out->frame_count; fi++) {
			size_t target = (size_t)(((uint64_t)(fi + 1) * out->all_pcm16_count) / out->frame_count);
			size_t n = target - pcm_cursor;
			VqaDecodedFrame *fr = &out->frames[fi];
			if (n) {
				fr->pcm16 = (int16_t *)malloc(n * sizeof(int16_t));
				if (!fr->pcm16)
					goto fail;
				memcpy(fr->pcm16, out->all_pcm16 + pcm_cursor, n * sizeof(int16_t));
				fr->pcm16_count = n;
				pcm_cursor = target;
			}
		}
	}

	vqa_reader_close(&d.r);
	free(d.cb_a);
	free(d.cb_b);
	free(d.vpt);
	return 0;

fail:
	vqa_reader_close(&d.r);
	free(d.cb_a);
	free(d.cb_b);
	free(d.vpt);
	free(d.pcm_accum);
	vqa_decode_free(out);
	return -1;
}
