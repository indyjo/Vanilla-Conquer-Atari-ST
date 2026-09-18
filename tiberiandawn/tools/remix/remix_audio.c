#include "remix_audio.h"

#include "remix.h"
#include "remix_detect.h"
#include "remix_unzap.h"
#include "st_host_resample.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct RemixFileReadCtx {
	FILE *f;
	long start;
	size_t remain;
};

enum { REMIX_OUT_BUF = 4096 };

struct RemixOutCtx {
	FILE *f;
	uint32_t payload_expected;
	uint32_t payload_written;
	unsigned char buf[REMIX_OUT_BUF];
	unsigned buf_len;
};

struct SrcPipe {
	StHostSrc *src;
	struct RemixOutCtx *out;
	uint32_t left;
	int16_t acc[512];
	unsigned acc_n;
};

static int remix_out_flush(struct RemixOutCtx *o)
{
	if (o->buf_len == 0)
		return 1;
	if (fwrite(o->buf, 1, o->buf_len, o->f) != (size_t)o->buf_len)
		return 0;
	o->payload_written += o->buf_len;
	o->buf_len = 0;
	return 1;
}

static int remix_out_push_u8(struct RemixOutCtx *o, unsigned char byte)
{
	o->buf[o->buf_len++] = byte;
	if (o->buf_len >= REMIX_OUT_BUF)
		return remix_out_flush(o);
	return 1;
}

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

static size_t remix_file_read_fn(void *ctx, unsigned char *dst, size_t max_len)
{
	struct RemixFileReadCtx *r = (struct RemixFileReadCtx *)ctx;
	size_t n;

	if (!r || !r->f || r->remain == 0)
		return 0;
	if (max_len > r->remain)
		max_len = r->remain;
	n = fread(dst, 1, max_len, r->f);
	r->remain -= n;
	return n;
}

int remix_aud_is_target(const unsigned char *hdr, size_t hdr_len)
{
	unsigned short rate;

	if (hdr_len < (size_t)REMIX_AUD_HDR_LEN || hdr[11] != REMIX_AUD_COMP_PCM)
		return 0;
	if ((hdr[10] & (REMIX_AUD_FLAG_STEREO | REMIX_AUD_FLAG_16BIT)) != 0)
		return 0;
	rate = read_le16(hdr);
	return rate == (unsigned short)REMIX_TARGET_RATE;
}

int remix_aud_needs_convert(const unsigned char *hdr, size_t hdr_len, uint32_t file_size)
{
	char type[20];

	if (hdr_len < (size_t)REMIX_AUD_HDR_LEN)
		return 0;
	if (!remix_looks_like_aud(hdr, hdr_len, file_size, type, sizeof(type)))
		return 0;
	if (remix_aud_is_target(hdr, hdr_len))
		return 0;
	return 1;
}

static int remix_out_begin(struct RemixOutCtx *o, FILE *outf, uint32_t payload_bytes)
{
	unsigned char hdr[REMIX_AUD_HDR_LEN];

	memset(o, 0, sizeof(*o));
	o->f = outf;
	o->payload_expected = payload_bytes;
	write_le16(hdr, REMIX_TARGET_RATE);
	write_le32(hdr + 2, payload_bytes);
	write_le32(hdr + 6, payload_bytes);
	hdr[10] = 0;
	hdr[11] = REMIX_AUD_COMP_PCM;
	return fwrite(hdr, 1, REMIX_AUD_HDR_LEN, outf) == REMIX_AUD_HDR_LEN;
}

static int remix_out_finish(struct RemixOutCtx *o)
{
	if (!remix_out_flush(o))
		return 0;
	return o->payload_written == o->payload_expected;
}

static int pipe_pull_out(struct SrcPipe *p)
{
	unsigned char ubuf[ST_HOST_SRC_RING];
	unsigned got, i;

	if (p->left == 0)
		return 1;
	got = st_host_src_pull_u8(p->src, ubuf, p->left < ST_HOST_SRC_RING ? (unsigned)p->left : ST_HOST_SRC_RING);
	if (!got)
		return 0;
	for (i = 0; i < got; i++) {
		if (!remix_out_push_u8(p->out, ubuf[i]))
			return -1;
	}
	p->left -= got;
	return 1;
}

static int pipe_push_s16(struct SrcPipe *p, const int16_t *pcm, unsigned n)
{
	unsigned off = 0;

	while (off < n) {
		unsigned sp = st_host_src_in_space(p->src);
		unsigned chunk;
		int pr;

		if (sp == 0) {
			pr = pipe_pull_out(p);
			if (pr <= 0)
				return 0;
			continue;
		}
		chunk = n - off;
		if (chunk > sp)
			chunk = sp;
		if (!st_host_src_push_s16(p->src, pcm + off, chunk))
			return 0;
		off += chunk;
	}
	return 1;
}

static int pipe_push_s8_as_s16(struct SrcPipe *p, signed char s8)
{
	int16_t v = (int16_t)((int)s8 << 8);

	p->acc[p->acc_n++] = v;
	if (p->acc_n == 512u)
		return pipe_push_s16(p, p->acc, 512u) ? (p->acc_n = 0, 1) : 0;
	return 1;
}

static int pipe_flush_acc(struct SrcPipe *p)
{
	if (!p->acc_n)
		return 1;
	if (!pipe_push_s16(p, p->acc, p->acc_n))
		return 0;
	p->acc_n = 0;
	return 1;
}

static int pipe_finish(struct SrcPipe *p)
{
	int pr;

	if (!pipe_flush_acc(p))
		return 0;
	if (st_host_src_finish(p->src) != 1)
		return 0;
	while (p->left) {
		pr = pipe_pull_out(p);
		if (pr < 0)
			return 0;
		if (pr == 0) {
			while (p->left) {
				if (!remix_out_push_u8(p->out, 128))
					return 0;
				p->left -= 1;
			}
			break;
		}
	}
	{
		unsigned char dump[256];
		while (st_host_src_pull_u8(p->src, dump, 256u))
			;
	}
	return 1;
}

static int unzap_write_fn(void *ctx, unsigned char sample)
{
	struct SrcPipe *p = (struct SrcPipe *)ctx;
	signed char s8 = (signed char)((int)sample - 128);
	return pipe_push_s8_as_s16(p, s8);
}

static int convert_westwood(
    const unsigned char *hdr, struct RemixFileReadCtx *read, FILE *out,
    RemixProgressFn progress, void *progress_ctx, uint32_t *out_bytes)
{
	struct RemixOutCtx octx;
	StHostSrc src;
	struct SrcPipe pipe;
	unsigned long comp, uncomp, even_in;
	unsigned in_rate;
	uint32_t payload;
	unsigned char *comp_buf = NULL;
	int ok = 0;

	comp = read_le32(hdr + 2);
	uncomp = read_le32(hdr + 6);
	in_rate = st_host_normalize_rate(read_le16(hdr));
	even_in = uncomp & ~1UL;
	payload = st_host_predicted_dest((uint32_t)even_in, in_rate);
	if (!payload || !in_rate)
		return 0;

	comp_buf = (unsigned char *)malloc((size_t)comp);
	if (!comp_buf)
		return 0;
	if (remix_file_read_fn(read, comp_buf, (size_t)comp) != (size_t)comp)
		goto done;
	memset(&src, 0, sizeof(src));
	if (!st_host_src_open(&src, in_rate))
		goto done;
	if (!remix_out_begin(&octx, out, payload)) {
		st_host_src_close(&src);
		goto done;
	}
	memset(&pipe, 0, sizeof(pipe));
	pipe.src = &src;
	pipe.out = &octx;
	pipe.left = payload;
	if (!remix_unzap_stream(comp_buf, (size_t)comp, (size_t)uncomp, unzap_write_fn, &pipe))
		goto close;
	if (!pipe_finish(&pipe))
		goto close;
	if (!remix_out_finish(&octx))
		goto close;
	if (progress)
		progress(progress_ctx, "CONVERT", 1, 1);
	*out_bytes = REMIX_AUD_HDR_LEN + octx.payload_written;
	ok = 1;
close:
	st_host_src_close(&src);
done:
	free(comp_buf);
	return ok;
}

static int convert_ima(
    const unsigned char *hdr, uint32_t file_size, struct RemixFileReadCtx *read, FILE *out,
    RemixProgressFn progress, void *progress_ctx, uint32_t *out_bytes)
{
	RemixImaCtx *ima = NULL;
	struct RemixOutCtx octx;
	StHostSrc src;
	struct SrcPipe pipe;
	unsigned long total, emitted, comp_len;
	unsigned in_rate;
	uint32_t payload;
	int16_t *s16 = NULL;
	unsigned scratch_cap = 0;
	int ok = 0;

	comp_len = read_le32(hdr + 2);
	in_rate = st_host_normalize_rate(read_le16(hdr));
	ima = remix_ima_stream_create(hdr, file_size, comp_len, remix_file_read_fn, read);
	if (!ima)
		return 0;
	total = remix_ima_stream_total_samples(ima) & ~1UL;
	payload = st_host_predicted_dest((uint32_t)total, in_rate);
	if (total == 0 || !payload || !in_rate)
		goto close_ima;

	memset(&src, 0, sizeof(src));
	if (!st_host_src_open(&src, in_rate))
		goto close_ima;
	if (!remix_out_begin(&octx, out, payload)) {
		st_host_src_close(&src);
		goto close_ima;
	}
	memset(&pipe, 0, sizeof(pipe));
	pipe.src = &src;
	pipe.out = &octx;
	pipe.left = payload;

	emitted = 0;
	while (emitted < total) {
		unsigned pending, got;

		pending = remix_ima_stream_pending_frame_samples(ima);
		if (pending == 0)
			goto close_src;
		if (pending > scratch_cap) {
			int16_t *n16 = (int16_t *)realloc(s16, (size_t)pending * sizeof(int16_t));
			if (!n16)
				goto close_src;
			s16 = n16;
			scratch_cap = pending;
		}
		got = remix_ima_stream_pull_s16(ima, s16, pending);
		if (got == 0 || got != pending)
			goto close_src;
		if (emitted + got > total)
			got = (unsigned)(total - emitted);
		if (!pipe_push_s16(&pipe, s16, got))
			goto close_src;
		emitted += got;
		if (progress)
			progress(progress_ctx, "CONVERT", (unsigned)emitted, (unsigned)total);
	}

	if (!pipe_finish(&pipe) || !remix_out_finish(&octx))
		goto close_src;
	*out_bytes = REMIX_AUD_HDR_LEN + octx.payload_written;
	ok = 1;
close_src:
	st_host_src_close(&src);
close_ima:
	free(s16);
	remix_ima_stream_destroy(ima);
	return ok;
}

static int convert_pcm(
    const unsigned char *hdr, struct RemixFileReadCtx *read, FILE *out,
    RemixProgressFn progress, void *progress_ctx, uint32_t *out_bytes)
{
	struct RemixOutCtx octx;
	StHostSrc src;
	struct SrcPipe pipe;
	unsigned in_rate;
	unsigned long uncomp, frames, even_in, frame_done = 0;
	unsigned stride;
	uint32_t payload;
	unsigned char ibuf[4096];
	int16_t s16[1024];
	int stereo, sixteen;
	int ok = 0;

	if (hdr[11] != REMIX_AUD_COMP_PCM)
		return 0;
	in_rate = st_host_normalize_rate(read_le16(hdr));
	stereo = (hdr[10] & REMIX_AUD_FLAG_STEREO) != 0;
	sixteen = (hdr[10] & REMIX_AUD_FLAG_16BIT) != 0;
	uncomp = read_le32(hdr + 6);
	stride = 1u;
	if (sixteen)
		stride = 2u;
	if (stereo)
		stride *= 2u;
	if (stride == 0 || uncomp < stride || !in_rate)
		return 0;
	frames = uncomp / stride;
	even_in = frames & ~1UL;
	payload = st_host_predicted_dest((uint32_t)even_in, in_rate);
	if (!payload)
		return 0;

	memset(&src, 0, sizeof(src));
	if (!st_host_src_open(&src, in_rate))
		return 0;
	if (!remix_out_begin(&octx, out, payload)) {
		st_host_src_close(&src);
		return 0;
	}
	memset(&pipe, 0, sizeof(pipe));
	pipe.src = &src;
	pipe.out = &octx;
	pipe.left = payload;

	while (frame_done < even_in) {
		size_t chunk_frames, chunk_bytes, got, i;
		unsigned n16 = 0;

		chunk_frames = even_in - frame_done;
		if (chunk_frames > sizeof(ibuf) / stride)
			chunk_frames = sizeof(ibuf) / stride;
		chunk_bytes = chunk_frames * stride;
		got = remix_file_read_fn(read, ibuf, chunk_bytes);
		if (got < stride)
			goto done_pcm;
		chunk_frames = got / stride;
		if (frame_done + chunk_frames > even_in)
			chunk_frames = (size_t)(even_in - frame_done);

		for (i = 0; i < chunk_frames; ++i) {
			unsigned char const *p = ibuf + i * stride;
			int sample;

			if (sixteen && stereo) {
				int lv = (int)(int16_t)(uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
				int rv = (int)(int16_t)(uint16_t)((uint16_t)p[2] | ((uint16_t)p[3] << 8));
				sample = (lv + rv) / 2;
			} else if (sixteen) {
				sample = (int)(int16_t)(uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
			} else if (stereo) {
				sample = ((((int)p[0] - 128) + ((int)p[1] - 128)) / 2) << 8;
			} else {
				sample = ((int)p[0] - 128) << 8;
			}
			if (sample > 32767)
				sample = 32767;
			if (sample < -32768)
				sample = -32768;
			s16[n16++] = (int16_t)sample;
			if (n16 == 1024u) {
				if (!pipe_push_s16(&pipe, s16, n16))
					goto done_pcm;
				n16 = 0;
			}
		}
		if (n16 && !pipe_push_s16(&pipe, s16, n16))
			goto done_pcm;
		frame_done += (unsigned long)chunk_frames;
		if (progress)
			progress(progress_ctx, "CONVERT", (unsigned)frame_done, (unsigned)even_in);
	}

	if (!pipe_finish(&pipe) || !remix_out_finish(&octx))
		goto done_pcm;
	*out_bytes = REMIX_AUD_HDR_LEN + octx.payload_written;
	ok = 1;
done_pcm:
	st_host_src_close(&src);
	return ok;
}

int remix_audio_stream_convert(
    const unsigned char *probe, size_t probe_len, uint32_t file_size,
    RemixPayloadReadFn read_fn, void *read_ctx, long payload_start, FILE *in,
    FILE *out, RemixProgressFn progress, void *progress_ctx, uint32_t *out_bytes)
{
	struct RemixFileReadCtx fctx;
	const unsigned char *hdr;
	unsigned char hdr_buf[REMIX_AUD_HDR_LEN];
	unsigned long payload_len;
	int rc = 0;

	(void)read_fn;
	(void)read_ctx;
	if (!probe || probe_len < (size_t)REMIX_AUD_HDR_LEN || !in || !out || !out_bytes)
		return 0;

	if (probe_len >= (size_t)REMIX_AUD_HDR_LEN)
		hdr = probe;
	else {
		memcpy(hdr_buf, probe, probe_len);
		if (fread(hdr_buf + probe_len, 1, REMIX_AUD_HDR_LEN - probe_len, in) != REMIX_AUD_HDR_LEN - probe_len)
			return 0;
		hdr = hdr_buf;
	}

	{
		size_t aud_probe_len = (hdr == probe) ? probe_len : (size_t)REMIX_AUD_HDR_LEN;

		if (!remix_aud_needs_convert(hdr, aud_probe_len, file_size))
			return 0;
	}

	payload_len = read_le32(hdr + 2);
	if (payload_len == 0 || payload_len > file_size - REMIX_AUD_HDR_LEN)
		payload_len = file_size - REMIX_AUD_HDR_LEN;

	memset(&fctx, 0, sizeof(fctx));
	fctx.f = in;
	fctx.start = payload_start + REMIX_AUD_HDR_LEN;
	fctx.remain = (size_t)payload_len;
	if (fseek(in, fctx.start, SEEK_SET) != 0)
		return 0;

	if (hdr[11] == REMIX_AUD_COMP_IMA99)
		rc = convert_ima(hdr, file_size, &fctx, out, progress, progress_ctx, out_bytes);
	else if (hdr[11] == REMIX_AUD_COMP_WESTWOOD)
		rc = convert_westwood(hdr, &fctx, out, progress, progress_ctx, out_bytes);
	else if (hdr[11] == REMIX_AUD_COMP_PCM)
		rc = convert_pcm(hdr, &fctx, out, progress, progress_ctx, out_bytes);

	return rc;
}
