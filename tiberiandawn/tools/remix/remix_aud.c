/*
 * AUD99 (Westwood IMA) decode and PCM rewrite for Atari STE playback.
 * Logic mirrors tiberiandawn/atarilib/audio_ste.cpp Sample_Make_PCM().
 */

#include "remix_aud.h"

#include <stdlib.h>
#include <string.h>

struct WsAdpcmState {
	int predictor;
	short step_index;
};

struct Ima99Core {
	const unsigned char *pay;
	unsigned long pay_len;
	const unsigned char *next_hdr;
	struct WsAdpcmState ws_mono;
	const unsigned char *frame_comp_base;
	unsigned frame_comp_len;
	unsigned frame_comp_off;
	unsigned frame_samples_total;
	unsigned frame_samples_emitted;
};

struct Ima99Stream {
	struct Ima99Core ima;
	unsigned long total_output_samples;
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

static void write_le16(unsigned char *p, unsigned short v)
{
	p[0] = (unsigned char)(v & 0xFFu);
	p[1] = (unsigned char)((v >> 8) & 0xFFu);
}

static void write_le32(unsigned char *p, unsigned long v)
{
	p[0] = (unsigned char)(v & 0xFFu);
	p[1] = (unsigned char)((v >> 8) & 0xFFu);
	p[2] = (unsigned char)((v >> 16) & 0xFFu);
	p[3] = (unsigned char)((v >> 24) & 0xFFu);
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
	unsigned char const *s;
	signed char *d;
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
		d = dst;
		s = src;
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

static void ima99_reset(struct Ima99Stream *s)
{
	memset(s, 0, sizeof(*s));
}

static void ima99_stream_init(struct Ima99Stream *s, const unsigned char *payload, unsigned long payload_len)
{
	memset(&s->ima, 0, sizeof(s->ima));
	s->ima.pay = payload;
	s->ima.pay_len = payload_len;
	s->ima.next_hdr = payload;
}

static int ima99_bind_from_aud(struct Ima99Stream *s, const unsigned char *aud, unsigned long aud_bytes)
{
	unsigned long payload_len;
	unsigned long pcm_cap;
	unsigned long payload_avail;
	unsigned long src_samples;
	const unsigned char *payload;

	ima99_reset(s);
	if (aud_bytes < (unsigned long)REMIX_AUD_HDR_LEN || aud == NULL)
		return 0;
	if (aud[11] != REMIX_AUD_COMP_IMA99)
		return 0;
	if ((aud[10] & REMIX_AUD_FLAG_STEREO) != 0)
		return 0;

	{
		unsigned long const size_file = read_le32(aud + 2);
		unsigned long const uncomp = read_le32(aud + 6);
		unsigned const aud_stride = (aud[10] & REMIX_AUD_FLAG_16BIT) ? 2u : 1u;

		if (size_file == 0UL || size_file > REMIX_AUD99_MAX_COMPRESSED_PAYLOAD || uncomp == 0UL
		    || (uncomp & 1UL) != 0UL || uncomp > REMIX_AUD99_MAX_DECODED_PCM_BYTES)
			return 0;
		pcm_cap = uncomp;
		if (pcm_cap == 0UL || pcm_cap > REMIX_AUD99_MAX_DECODED_PCM_BYTES)
			return 0;
		payload_avail = aud_bytes > (unsigned long)REMIX_AUD_HDR_LEN
		    ? aud_bytes - (unsigned long)REMIX_AUD_HDR_LEN
		    : 0;
		payload_len = size_file;
		if (payload_len > payload_avail)
			payload_len = payload_avail;
		if (payload_len == 0UL)
			return 0;
		src_samples = pcm_cap / aud_stride;
		if (src_samples == 0UL)
			return 0;
		payload = aud + REMIX_AUD_HDR_LEN;
		ima99_stream_init(s, payload, payload_len);
		s->total_output_samples = src_samples;
	}
	return 1;
}

static int ima99_open_next_frame(struct Ima99Stream *s)
{
	struct Ima99Core *ima = &s->ima;
	const unsigned char *const pay_end = ima->pay + ima->pay_len;
	unsigned comp;
	unsigned decomp;
	unsigned magic;
	unsigned frame_pcm;
	unsigned cap;

	if (ima->next_hdr + 8 > pay_end)
		return 0;
	comp = read_le16(ima->next_hdr);
	decomp = read_le16(ima->next_hdr + 2);
	magic = (unsigned)read_le32(ima->next_hdr + 4);
	if (magic != REMIX_AUD99_FRAME_MAGIC || comp == 0 || decomp == 0 || (decomp & 1u) != 0
	    || ima->next_hdr + 8 + comp > pay_end)
		return 0;

	frame_pcm = decomp;
	cap = comp * 4u;
	if (frame_pcm > cap)
		frame_pcm = cap;
	if (frame_pcm == 0 || (frame_pcm & 1u) != 0 || frame_pcm > REMIX_AUD99_MAX_SINGLE_FRAME_PCM)
		return 0;

	{
		const unsigned char *const chunk = ima->next_hdr + 8;
		ima->next_hdr += 8 + comp;
		ima->frame_comp_base = chunk;
	}
	ima->frame_comp_len = comp;
	ima->frame_comp_off = 0;
	ima->frame_samples_total = frame_pcm >> 1;
	ima->frame_samples_emitted = 0;
	return 1;
}

static unsigned ima99_stream_pull(struct Ima99Stream *s, signed char *dst, unsigned max_out)
{
	unsigned written = 0;

	while (written < max_out) {
		struct Ima99Core *ima = &s->ima;
		unsigned need;
		unsigned rem_samples;
		unsigned n;
		unsigned comp_left;
		const unsigned char *csrc;
		unsigned src_used;
		unsigned produced;

		if (ima->frame_samples_emitted >= ima->frame_samples_total) {
			ima->frame_comp_off = ima->frame_comp_len;
			if (!ima99_open_next_frame(s))
				break;
		}

		need = max_out - written;
		rem_samples = ima->frame_samples_total - ima->frame_samples_emitted;
		n = need < rem_samples ? need : rem_samples;
		n &= ~1u;
		if (n == 0)
			break;

		comp_left = ima->frame_comp_len - ima->frame_comp_off;
		csrc = ima->frame_comp_base + ima->frame_comp_off;
		src_used = 0;
		produced = ws_adpcm_decode_mono8(
		    &ima->ws_mono, csrc, comp_left, dst + written, n, &src_used);
		ima->frame_comp_off += src_used;
		ima->frame_samples_emitted += produced;
		written += produced;
		if (produced == 0)
			break;
	}
	return written;
}

static unsigned long ima99_skip(struct Ima99Stream *s, unsigned long sample_count)
{
	unsigned long skipped = 0;
	unsigned long left = sample_count;

	while (left > 0UL) {
		unsigned batch = left > (unsigned long)REMIX_IMA_SKIP_SCRATCH
		    ? (unsigned)REMIX_IMA_SKIP_SCRATCH
		    : (unsigned)left;
		unsigned got;

		batch &= ~1u;
		if (batch == 0)
			break;
		got = ima99_stream_pull(s, s->skip_scratch, batch);
		if (got == 0)
			break;
		skipped += (unsigned long)got;
		left -= (unsigned long)got;
	}
	return skipped;
}

int remix_is_aud99(const unsigned char *data, size_t len)
{
	struct Ima99Stream probe;

	if (len < (size_t)REMIX_AUD_HDR_LEN)
		return 0;
	if (data[11] != REMIX_AUD_COMP_IMA99)
		return 0;
	return ima99_bind_from_aud(&probe, data, (unsigned long)len);
}

int remix_convert_aud99(const unsigned char *in, size_t in_len, unsigned char **out_buf, size_t *out_len)
{
	struct Ima99Stream convert;
	unsigned long total_samples;
	unsigned long convert_samples;
	unsigned char scratch[REMIX_AUDIO_PULL_BLOCK];
	unsigned char *out;
	unsigned long written;
	unsigned long left;
	unsigned short rate;
	unsigned long payload_bytes;
	unsigned long out_cap;
	unsigned long out_size;

	if (!in || !out_buf || !out_len)
		return 0;
	*out_buf = NULL;
	*out_len = 0;

	if (!remix_is_aud99(in, in_len))
		return 0;

	if (!ima99_bind_from_aud(&convert, in, (unsigned long)in_len))
		return 0;

	total_samples = convert.total_output_samples;
	convert_samples = total_samples & ~1UL;
	if (convert_samples == 0UL || ima99_skip(&convert, convert_samples) != convert_samples)
		return 0;

	if (!ima99_bind_from_aud(&convert, in, (unsigned long)in_len))
		return 0;

	/* Worst case: header + one byte per two decoded samples. */
	out_cap = (unsigned long)REMIX_AUD_HDR_LEN + (convert_samples >> 1) + 16UL;
	out = (unsigned char *)malloc(out_cap);
	if (!out)
		return 0;

	memcpy(out, in, (size_t)REMIX_AUD_HDR_LEN);
	written = 0;
	left = convert_samples;
	while (left > 0UL) {
		unsigned long batch = left > (unsigned long)REMIX_AUDIO_PULL_BLOCK
		    ? (unsigned long)REMIX_AUDIO_PULL_BLOCK
		    : left;
		unsigned long i;
		unsigned long got;

		batch &= ~1UL;
		if (batch == 0)
			goto fail;
		got = (unsigned long)ima99_stream_pull(&convert, (signed char *)scratch, (unsigned)batch);
		if (got != batch)
			goto fail;
		for (i = 0; i < got; i += 2) {
			int const a = (int)(signed char)scratch[i];
			int const b = (int)(signed char)scratch[i + 1];
			int const avg = (a + b) / 2;
			out[(size_t)REMIX_AUD_HDR_LEN + (size_t)written++] =
			    (unsigned char)(avg + 128);
		}
		left -= got;
	}

	rate = read_le16(in);
	if (rate > 1u)
		rate = (unsigned short)(rate / 2u);

	payload_bytes = written;
	out_size = (unsigned long)REMIX_AUD_HDR_LEN + payload_bytes;
	if (out_size > out_cap)
		goto fail;

	write_le16(out, rate);
	write_le32(out + 2, payload_bytes);
	write_le32(out + 6, payload_bytes);
	out[10] = REMIX_AUD_FLAG_DUP2X;
	out[11] = REMIX_AUD_COMP_PCM;

	*out_buf = out;
	*out_len = (size_t)out_size;
	return 1;

fail:
	free(out);
	return 0;
}
