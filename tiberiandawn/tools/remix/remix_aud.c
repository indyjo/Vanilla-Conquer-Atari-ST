/*
 * AUD99 (Westwood IMA) decode for remix tooling.
 */

#include "remix_aud.h"

#include <stdlib.h>
#include <string.h>

struct WsAdpcmState {
	int predictor;
	short step_index;
};

struct Ima99Core {
	RemixAudReadFn read_fn;
	void *read_ctx;
	unsigned char *comp_buf;
	size_t comp_cap;
	size_t comp_len;
	size_t comp_pos;
	const unsigned char *next_hdr;
	unsigned long pay_remain;
	struct WsAdpcmState ws_l;
	struct WsAdpcmState ws_r;
	unsigned channels;
	unsigned frame_comp_len;
	unsigned frame_comp_off;
	unsigned frame_samples_total;
	unsigned frame_samples_emitted;
	unsigned char frame_hdr[8];
	int have_frame_hdr;
};

struct Ima99Stream {
	struct Ima99Core ima;
	unsigned long total_output_samples;
	unsigned long samples_emitted;
	signed char skip_scratch[REMIX_IMA_SKIP_SCRATCH];
};

static unsigned short read_le16(const unsigned char *p)
{
	return (unsigned short)((unsigned short)p[0] | ((unsigned short)p[1] << 8));
}

static unsigned long read_le32(const unsigned char *p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static int clamp16(int x)
{
	if (x > 32767)
		return 32767;
	if (x < -32768)
		return -32768;
	return x;
}

static int clamp_step(int x)
{
	if (x > 88)
		return 88;
	if (x < 0)
		return 0;
	return x;
}

static signed char pred_to_s8(int pred)
{
	return (signed char)(pred >> 8);
}

static short const kImaStepTable[89] = {
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
	337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707,
	1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845,
	8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
	32767
};

static signed char const kImaIndexTable[16] = {
	-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8,
};

static int g_delta_ready;
static int g_delta_table[89 * 16];

static void ws_adpcm_init_tables(void)
{
	unsigned si;
	unsigned nib;

	if (g_delta_ready)
		return;
	for (si = 0; si < 89; ++si) {
		int const step = (int)kImaStepTable[si];
		for (nib = 0; nib < 16; ++nib) {
			int const delta = (int)(nib & 7u);
			int diff = ((2 * delta + 1) * step) >> 3;
			if (nib & 8)
				diff = -diff;
			g_delta_table[si * 16 + nib] = diff;
		}
	}
	g_delta_ready = 1;
}

static void apply_nibble_mono8(unsigned nib, int *pred, int *si, signed char **d)
{
	unsigned const n = nib & 15u;

	*pred += g_delta_table[((unsigned)*si << 4) + n];
	*pred = clamp16(*pred);
	*si = clamp_step(*si + (int)kImaIndexTable[n]);
	*(*d)++ = pred_to_s8(*pred);
}

static unsigned ws_adpcm_decode_mono8(
    struct WsAdpcmState *st, const unsigned char *src, unsigned src_bytes,
    signed char *dst, unsigned dst_samples, unsigned *out_src_bytes_used)
{
	unsigned nbytes;
	unsigned p;

	if (out_src_bytes_used)
		*out_src_bytes_used = 0;
	if (!st || !dst || dst_samples == 0 || (dst_samples & 1u) != 0u)
		return 0;
	if (!src && src_bytes > 0)
		return 0;
	if (!g_delta_ready)
		ws_adpcm_init_tables();

	nbytes = dst_samples >> 1;
	if (nbytes > src_bytes)
		nbytes = src_bytes;
	if (nbytes == 0)
		return 0;

	{
		int pred = st->predictor;
		int si = clamp_step((int)st->step_index);
		signed char *d = dst;
		const unsigned char *s = src;

		p = nbytes;
		while (p--) {
			unsigned char const b = *s++;
			apply_nibble_mono8((unsigned)(b & 15u), &pred, &si, &d);
			apply_nibble_mono8(((unsigned)b >> 4) & 15u, &pred, &si, &d);
		}
		st->predictor = pred;
		st->step_index = (short)si;
	}
	if (out_src_bytes_used)
		*out_src_bytes_used = nbytes;
	return nbytes << 1;
}

static void ima_compact(struct Ima99Core *ima)
{
	size_t remain;

	if (ima->comp_pos == 0)
		return;
	if (ima->comp_pos >= ima->comp_len) {
		ima->comp_pos = 0;
		ima->comp_len = 0;
		return;
	}
	remain = ima->comp_len - ima->comp_pos;
	memmove(ima->comp_buf, ima->comp_buf + ima->comp_pos, remain);
	ima->comp_len = remain;
	ima->comp_pos = 0;
}

static size_t ima_read_more(struct Ima99Core *ima, size_t need)
{
	size_t got;
	unsigned char *nbuf;
	size_t ncap;

	if (!ima->read_fn || need == 0)
		return 0;
	if (ima->comp_len + need > ima->comp_cap) {
		ncap = ima->comp_cap ? ima->comp_cap * 2u : 4096u;
		while (ncap < ima->comp_len + need)
			ncap *= 2u;
		nbuf = (unsigned char *)realloc(ima->comp_buf, ncap);
		if (!nbuf)
			return 0;
		ima->comp_buf = nbuf;
		ima->comp_cap = ncap;
	}
	got = ima->read_fn(ima->read_ctx, ima->comp_buf + ima->comp_len, need);
	if (got == 0)
		return 0;
	ima->comp_len += got;
	return got;
}

static int ima_ensure_bytes(struct Ima99Core *ima, size_t need)
{
	while (ima->comp_len - ima->comp_pos < need && ima->pay_remain > 0) {
		size_t avail = ima->comp_len - ima->comp_pos;
		size_t want = need - avail;

		if (want > ima->pay_remain)
			want = (size_t)ima->pay_remain;
		if (want > 8192u)
			want = 8192u;
		if (want == 0)
			want = 1;
		{
			size_t got = ima_read_more(ima, want);
			if (got == 0)
				return 0;
			ima->pay_remain -= got;
		}
	}
	return ima->comp_len - ima->comp_pos >= need;
}

static int ima_load_frame(struct Ima99Core *ima)
{
	unsigned comp;
	unsigned decomp;
	unsigned magic;
	unsigned frame_pcm;
	unsigned cap;

	ima->frame_comp_off = 0;
	ima->frame_samples_emitted = 0;
	ima->have_frame_hdr = 0;

	if (!ima_ensure_bytes(ima, 8))
		return 0;

	memcpy(ima->frame_hdr, ima->comp_buf + ima->comp_pos, 8);
	comp = read_le16(ima->frame_hdr);
	decomp = read_le16(ima->frame_hdr + 2);
	magic = (unsigned)read_le32(ima->frame_hdr + 4);
	if (magic != REMIX_AUD99_FRAME_MAGIC || comp == 0 || decomp == 0 || (decomp & 1u) != 0)
		return 0;

	frame_pcm = decomp;
	cap = comp * 4u;
	if (frame_pcm > cap)
		frame_pcm = cap;
	if (frame_pcm == 0 || (frame_pcm & 1u) != 0 || frame_pcm > REMIX_AUD99_MAX_SINGLE_FRAME_PCM)
		return 0;

	if (!ima_ensure_bytes(ima, 8u + (size_t)comp))
		return 0;

	ima->comp_pos += 8;
	ima->frame_comp_len = comp;
	ima->frame_samples_total = frame_pcm >> 1;
	ima->have_frame_hdr = 1;
	return 1;
}

static int ima_advance_frame(struct Ima99Core *ima)
{
	if (ima->have_frame_hdr) {
		ima->comp_pos += ima->frame_comp_off;
		ima->frame_comp_off = 0;
		ima->have_frame_hdr = 0;
		ima_compact(ima);
	}
	return ima_load_frame(ima);
}

static int ima_ensure_frame(struct Ima99Core *ima)
{
	if (ima->have_frame_hdr && ima->frame_samples_emitted < ima->frame_samples_total)
		return 1;
	return ima_advance_frame(ima);
}

static unsigned ima_pull_mono(struct Ima99Core *ima, signed char *dst, unsigned max_out)
{
	unsigned written = 0;

	while (written < max_out) {
		unsigned need;
		unsigned rem;
		unsigned n;
		unsigned comp_left;
		const unsigned char *csrc;
		unsigned src_used;
		unsigned produced;

		if (!ima_ensure_frame(ima))
			break;

		need = max_out - written;
		rem = ima->frame_samples_total - ima->frame_samples_emitted;
		n = need < rem ? need : rem;
		n &= ~1u;
		if (n == 0)
			break;

		comp_left = ima->frame_comp_len - ima->frame_comp_off;
		csrc = ima->comp_buf + ima->comp_pos + ima->frame_comp_off;
		src_used = 0;
		produced = ws_adpcm_decode_mono8(&ima->ws_l, csrc, comp_left, dst + written, n, &src_used);
		ima->frame_comp_off += src_used;
		ima->frame_samples_emitted += produced;
		written += produced;
		if (produced == 0)
			break;
	}
	return written;
}

static unsigned ima_pull_stereo(struct Ima99Core *ima, signed char *dst, unsigned max_out)
{
	unsigned written = 0;
	signed char lr[REMIX_AUDIO_PULL_BLOCK * 2];

	while (written < max_out) {
		unsigned need;
		unsigned rem;
		unsigned n;
		unsigned half;
		unsigned comp_left;
		const unsigned char *csrc;
		unsigned src_used_l;
		unsigned src_used_r;
		unsigned produced_l;
		unsigned produced_r;
		unsigned i;

		if (!ima_ensure_frame(ima))
			break;

		need = max_out - written;
		rem = ima->frame_samples_total - ima->frame_samples_emitted;
		n = need < rem ? need : rem;
		n &= ~1u;
		if (n == 0)
			break;
		if (n > REMIX_AUDIO_PULL_BLOCK)
			n = REMIX_AUDIO_PULL_BLOCK;

		half = ima->frame_comp_len / 2u;
		comp_left = half - ima->frame_comp_off;
		if (comp_left == 0)
			break;
		csrc = ima->comp_buf + ima->comp_pos + ima->frame_comp_off;
		produced_l = ws_adpcm_decode_mono8(&ima->ws_l, csrc, comp_left, lr, n, &src_used_l);
		produced_r = ws_adpcm_decode_mono8(
		    &ima->ws_r, csrc + half, comp_left, lr + n, n, &src_used_r);
		if (src_used_l != src_used_r || produced_l != produced_r || produced_l == 0)
			break;
		for (i = 0; i < produced_l; ++i)
			dst[written + i] = (signed char)(((int)lr[i] + (int)lr[n + i]) / 2);
		ima->frame_comp_off += src_used_l;
		ima->frame_samples_emitted += produced_l;
		written += produced_l;
	}
	return written;
}

static void ima99_reset(struct Ima99Stream *s)
{
	memset(s, 0, sizeof(*s));
}

static int ima99_bind_stream(
    struct Ima99Stream *s, const unsigned char *aud, unsigned long aud_bytes,
    RemixAudReadFn read_fn, void *read_ctx, unsigned long payload_len)
{
	unsigned long uncomp;
	unsigned aud_stride;
	unsigned long src_samples;

	ima99_reset(s);
	if (aud_bytes < (unsigned long)REMIX_AUD_HDR_LEN || aud == NULL || aud[11] != REMIX_AUD_COMP_IMA99)
		return 0;

	{
		unsigned long const size_file = read_le32(aud + 2);
		unsigned char const flags = aud[10];

		uncomp = read_le32(aud + 6);
		aud_stride = (flags & REMIX_AUD_FLAG_16BIT) ? 2u : 1u;
		if (size_file == 0UL || size_file > REMIX_AUD99_MAX_COMPRESSED_PAYLOAD || uncomp == 0UL
		    || uncomp > REMIX_AUD99_MAX_DECODED_PCM_BYTES)
			return 0;
		/* Some retail tracks store an odd uncomp byte count; drop a trailing odd byte. */
		if ((uncomp & 1UL) != 0UL)
			uncomp -= 1UL;
		if (uncomp < aud_stride)
			return 0;
		if (payload_len == 0UL || payload_len > size_file)
			payload_len = size_file;
		src_samples = uncomp / aud_stride;
		if ((flags & REMIX_AUD_FLAG_STEREO) != 0)
			src_samples /= 2u;
		if (src_samples == 0UL)
			return 0;
		s->ima.read_fn = read_fn;
		s->ima.read_ctx = read_ctx;
		s->ima.pay_remain = payload_len;
		s->ima.channels = ((flags & REMIX_AUD_FLAG_STEREO) != 0) ? 2u : 1u;
		s->total_output_samples = src_samples;
	}
	return 1;
}

static unsigned ima99_stream_pull(struct Ima99Stream *s, signed char *dst, unsigned max_out)
{
	if (s->ima.channels == 2)
		return ima_pull_stereo(&s->ima, dst, max_out);
	return ima_pull_mono(&s->ima, dst, max_out);
}

static void ima99_free(struct Ima99Stream *s)
{
	free(s->ima.comp_buf);
	s->ima.comp_buf = NULL;
	s->ima.comp_cap = 0;
	s->ima.comp_len = 0;
}

struct RemixImaCtx {
	struct Ima99Stream stream;
};

int remix_is_aud99(const unsigned char *data, size_t len)
{
	if (len < (size_t)REMIX_AUD_HDR_LEN || data[11] != REMIX_AUD_COMP_IMA99)
		return 0;
	if ((data[10] & REMIX_AUD_FLAG_STEREO) != 0)
		return 1;
	{
		unsigned long uncomp = read_le32(data + 6);
		unsigned const aud_stride = (data[10] & REMIX_AUD_FLAG_16BIT) ? 2u : 1u;
		if (uncomp == 0)
			return 0;
		if ((uncomp & 1u) != 0)
			uncomp -= 1u;
		if (uncomp < aud_stride || uncomp / aud_stride == 0)
			return 0;
	}
	return 1;
}

RemixImaCtx *remix_ima_stream_create(
    const unsigned char *aud, size_t aud_len, unsigned long payload_len,
    RemixAudReadFn read_fn, void *read_ctx)
{
	RemixImaCtx *ctx = (RemixImaCtx *)calloc(1, sizeof(RemixImaCtx));
	if (!ctx)
		return NULL;
	if (!ima99_bind_stream(&ctx->stream, aud, (unsigned long)aud_len, read_fn, read_ctx, payload_len)) {
		free(ctx);
		return NULL;
	}
	return ctx;
}

unsigned long remix_ima_stream_total_samples(const RemixImaCtx *ctx)
{
	if (!ctx)
		return 0;
	return ctx->stream.total_output_samples;
}

unsigned remix_ima_stream_pending_frame_samples(RemixImaCtx *ctx)
{
	struct Ima99Core *ima;

	if (!ctx)
		return 0;
	ima = &ctx->stream.ima;
	if (!ima_ensure_frame(ima))
		return 0;
	return ima->frame_samples_total - ima->frame_samples_emitted;
}

unsigned remix_ima_stream_pull_s8(RemixImaCtx *ctx, signed char *dst, unsigned max_out)
{
	if (!ctx)
		return 0;
	return ima99_stream_pull(&ctx->stream, dst, max_out);
}

void remix_ima_stream_destroy(RemixImaCtx *ctx)
{
	if (!ctx)
		return;
	ima99_free(&ctx->stream);
	free(ctx);
}

int remix_convert_aud99(const unsigned char *in, size_t in_len, unsigned char **out_buf, size_t *out_len)
{
	/* Built by remix_audio buffer path; kept for compatibility. */
	(void)in;
	(void)in_len;
	(void)out_buf;
	(void)out_len;
	return 0;
}
