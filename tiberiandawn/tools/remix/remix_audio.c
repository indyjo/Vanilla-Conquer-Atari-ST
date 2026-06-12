#include "remix_audio.h"

#include "remix.h"
#include "remix_detect.h"
#include "remix_unzap.h"

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
	struct {
		unsigned factor;
		unsigned phase;
		int count;
		int sum;
	} rs;
	uint32_t payload_expected;
	uint32_t payload_written;
	unsigned char buf[REMIX_OUT_BUF];
	unsigned buf_len;
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
	unsigned short rate;
	char type[20];

	(void)file_size;
	if (hdr_len < (size_t)REMIX_AUD_HDR_LEN)
		return 0;
	if (!remix_looks_like_aud(hdr, hdr_len, file_size, type, sizeof(type)))
		return 0;
	if (remix_aud_is_target(hdr, hdr_len))
		return 0;
	if (hdr[11] == REMIX_AUD_COMP_PCM && (hdr[10] & (REMIX_AUD_FLAG_STEREO | REMIX_AUD_FLAG_16BIT)) == 0) {
		rate = read_le16(hdr);
		if (rate > 0 && rate < (unsigned short)REMIX_TARGET_RATE)
			return 0;
	}
	return 1;
}

static int remix_resample_factor(unsigned rate, unsigned *factor_out)
{
	rate = remix_aud_normalize_rate((unsigned short)rate);
	if (rate < (unsigned)REMIX_TARGET_RATE)
		return 0;
	if (rate == (unsigned)REMIX_TARGET_RATE) {
		*factor_out = 1;
		return 1;
	}
	if (rate % (unsigned)REMIX_TARGET_RATE != 0)
		return -1;
	*factor_out = rate / (unsigned)REMIX_TARGET_RATE;
	return 1;
}

static uint32_t remix_resampled_payload_bytes(uint32_t samples, unsigned factor)
{
	if (factor <= 1)
		return samples;
	return samples / factor;
}

static int remix_out_write_u8(struct RemixOutCtx *o, unsigned char u8)
{
	unsigned char byte;

	if (o->rs.factor <= 1)
		return remix_out_push_u8(o, u8);

	{
		int s = (int)u8 - 128;
		o->rs.sum += s;
		++o->rs.count;
		++o->rs.phase;
		if (o->rs.phase < o->rs.factor)
			return 1;
		byte = (unsigned char)((o->rs.sum / (int)o->rs.factor) + 128);
		o->rs.phase = 0;
		o->rs.count = 0;
		o->rs.sum = 0;
		return remix_out_push_u8(o, byte);
	}
}

static int remix_out_write_s8(struct RemixOutCtx *o, signed char s8)
{
	return remix_out_write_u8(o, (unsigned char)((int)s8 + 128));
}

static int remix_out_write_s8_block(struct RemixOutCtx *o, const signed char *src, unsigned n)
{
	unsigned i = 0;

	if (o->rs.factor <= 1) {
		while (i < n) {
			unsigned space = REMIX_OUT_BUF - o->buf_len;
			unsigned chunk = n - i;

			if (chunk > space)
				chunk = space;
			for (; chunk > 0; --chunk)
				o->buf[o->buf_len++] = (unsigned char)((int)src[i++] + 128);
			if (o->buf_len >= REMIX_OUT_BUF && !remix_out_flush(o))
				return 0;
		}
		return 1;
	}

	if (o->rs.factor == 2) {
		while (i < n && o->rs.phase > 0) {
			if (!remix_out_write_s8(o, src[i++]))
				return 0;
		}
		while (i + 1 < n) {
			int avg = ((int)src[i] + (int)src[i + 1]) / 2;

			if (!remix_out_push_u8(o, (unsigned char)(avg + 128)))
				return 0;
			i += 2;
		}
		if (i < n) {
			if (!remix_out_write_s8(o, src[i]))
				return 0;
		}
		return 1;
	}

	for (; i < n; ++i) {
		if (!remix_out_write_s8(o, src[i]))
			return 0;
	}
	return 1;
}

static int remix_out_begin(
    struct RemixOutCtx *o, FILE *outf, unsigned resample_factor, uint32_t payload_bytes)
{
	unsigned char hdr[REMIX_AUD_HDR_LEN];

	memset(o, 0, sizeof(*o));
	o->f = outf;
	o->rs.factor = resample_factor;
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

struct UnzapOutCtx {
	struct RemixOutCtx *out;
};

static int unzap_write_fn(void *ctx, unsigned char sample)
{
	struct UnzapOutCtx *u = (struct UnzapOutCtx *)ctx;
	return remix_out_write_u8(u->out, sample);
}

static int convert_westwood(
    const unsigned char *hdr, struct RemixFileReadCtx *read, FILE *out,
    RemixProgressFn progress, void *progress_ctx, uint32_t *out_bytes)
{
	struct RemixOutCtx octx;
	struct UnzapOutCtx uz;
	unsigned long comp;
	unsigned long uncomp;
	unsigned resample_factor;
	unsigned char *comp_buf = NULL;
	int ok = 0;

	comp = read_le32(hdr + 2);
	uncomp = read_le32(hdr + 6);
	{
		int rf = remix_resample_factor(read_le16(hdr), &resample_factor);
		if (rf < 0)
			return 0;
		if (rf == 0)
			resample_factor = 1;
	}

	comp_buf = (unsigned char *)malloc((size_t)comp);
	if (!comp_buf)
		return 0;
	if (remix_file_read_fn(read, comp_buf, (size_t)comp) != (size_t)comp)
		goto done;

	if (!remix_out_begin(&octx, out, resample_factor, remix_resampled_payload_bytes((uint32_t)uncomp, resample_factor)))
		goto done;
	uz.out = &octx;
	if (!remix_unzap_stream(comp_buf, (size_t)comp, (size_t)uncomp, unzap_write_fn, &uz))
		goto done;
	if (!remix_out_finish(&octx))
		goto done;
	if (progress)
		progress(progress_ctx, "CONVERT", 1, 1);
	*out_bytes = REMIX_AUD_HDR_LEN + octx.payload_written;
	ok = 1;

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
	unsigned resample_factor;
	unsigned long total;
	unsigned long emitted;
	signed char *scratch = NULL;
	unsigned scratch_cap = 0;
	int ok = 0;
	unsigned long comp_len;
	uint32_t payload_bytes;

	comp_len = read_le32(hdr + 2);
	{
		int rf = remix_resample_factor(read_le16(hdr), &resample_factor);
		if (rf < 0)
			return 0;
		if (rf == 0)
			resample_factor = 1;
	}

	ima = remix_ima_stream_create(hdr, file_size, comp_len, remix_file_read_fn, read);
	if (!ima)
		return 0;
	total = remix_ima_stream_total_samples(ima) & ~1UL;
	if (total == 0)
		goto close_ima;

	payload_bytes = remix_resampled_payload_bytes((uint32_t)total, resample_factor);
	if (!remix_out_begin(&octx, out, resample_factor, payload_bytes))
		goto close_ima;

	emitted = 0;
	while (emitted < total) {
		unsigned pending;
		unsigned got;

		pending = remix_ima_stream_pending_frame_samples(ima);
		if (pending == 0)
			goto close_ima;
		if (pending > scratch_cap) {
			signed char *nbuf = (signed char *)realloc(scratch, (size_t)pending);

			if (!nbuf)
				goto close_ima;
			scratch = nbuf;
			scratch_cap = pending;
		}
		got = remix_ima_stream_pull_s8(ima, scratch, pending);
		if (got == 0 || got != pending)
			goto close_ima;
		if (!remix_out_write_s8_block(&octx, scratch, got))
			goto close_ima;
		emitted += got;
		if (progress)
			progress(progress_ctx, "CONVERT", (unsigned)emitted, (unsigned)total);
	}

	if (!remix_out_finish(&octx))
		goto close_ima;
	*out_bytes = REMIX_AUD_HDR_LEN + octx.payload_written;
	ok = 1;

close_ima:
	free(scratch);
	remix_ima_stream_destroy(ima);
	return ok;
}

static int convert_pcm(
    const unsigned char *hdr, struct RemixFileReadCtx *read, FILE *out,
    RemixProgressFn progress, void *progress_ctx, uint32_t *out_bytes)
{
	struct RemixOutCtx octx;
	unsigned resample_factor;
	unsigned long uncomp;
	unsigned stride;
	unsigned long frames;
	unsigned long frame_done = 0;
	unsigned char ibuf[4096];
	int stereo;
	int sixteen;
	int ok = 0;

	if (hdr[11] != REMIX_AUD_COMP_PCM)
		return 0;
	{
		int rf = remix_resample_factor(read_le16(hdr), &resample_factor);
		if (rf < 0)
			return 0;
		if (rf == 0)
			resample_factor = 1;
	}

	stereo = (hdr[10] & REMIX_AUD_FLAG_STEREO) != 0;
	sixteen = (hdr[10] & REMIX_AUD_FLAG_16BIT) != 0;
	uncomp = read_le32(hdr + 6);
	stride = 1u;
	if (sixteen)
		stride = 2u;
	if (stereo)
		stride *= 2u;
	if (stride == 0 || uncomp < stride)
		return 0;
	frames = uncomp / stride;

	if (!remix_out_begin(
	        &octx, out, resample_factor, remix_resampled_payload_bytes((uint32_t)frames, resample_factor)))
		return 0;

	while (frame_done < frames) {
		size_t chunk_frames;
		size_t chunk_bytes;
		size_t got;
		size_t i;

		chunk_frames = frames - frame_done;
		if (chunk_frames > sizeof(ibuf) / stride)
			chunk_frames = sizeof(ibuf) / stride;
		chunk_bytes = chunk_frames * stride;
		got = remix_file_read_fn(read, ibuf, chunk_bytes);
		if (got < stride)
			goto done_pcm;
		chunk_frames = got / stride;

		for (i = 0; i < chunk_frames; ++i) {
			unsigned char const *p = ibuf + i * stride;
			int sample;

			if (sixteen && stereo) {
				int lv = (int)(int16_t)(uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
				int rv = (int)(int16_t)(uint16_t)((uint16_t)p[2] | ((uint16_t)p[3] << 8));
				sample = ((lv + rv) / 2) >> 8;
			} else if (sixteen) {
				int v = (int)(int16_t)(uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
				sample = v >> 8;
			} else if (stereo) {
				sample = (((int)p[0] - 128) + ((int)p[1] - 128)) / 2;
			} else {
				sample = (int)p[0] - 128;
			}
			if (!remix_out_write_s8(&octx, (signed char)sample))
				goto done_pcm;
		}
		frame_done += (unsigned long)chunk_frames;
		if (progress)
			progress(progress_ctx, "CONVERT", (unsigned)frame_done, (unsigned)frames);
	}

	if (!remix_out_finish(&octx))
		goto done_pcm;
	*out_bytes = REMIX_AUD_HDR_LEN + octx.payload_written;
	ok = 1;

done_pcm:
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
