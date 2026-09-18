#include "st_host_resample.h"

#include "samplerate.h"

#include <math.h>
#include <string.h>

unsigned st_host_normalize_rate(unsigned rate)
{
	if (rate > 20000u && rate < 24000u)
		return 22050u;
	return rate;
}

uint32_t st_host_predicted_dest(uint32_t in_n, unsigned in_rate)
{
	uint32_t n;

	in_n &= ~1u;
	in_rate = st_host_normalize_rate(in_rate);
	if (!in_n || !in_rate)
		return 0;
	n = (uint32_t)(((uint64_t)in_n * (uint64_t)ST_HOST_DMA_RATE + (uint64_t)(in_rate / 2u)) / (uint64_t)in_rate);
	return n & ~1u;
}

static uint32_t lcg(uint32_t *s)
{
	*s = *s * 1664525u + 1013904223u;
	return *s;
}

static signed char quant_s8(StHostSrc *s, float x)
{
	float a, b, dith;
	int v;

	a = (float)(lcg(&s->dither) >> 8) * (1.0f / 16777216.0f);
	b = (float)(lcg(&s->dither) >> 8) * (1.0f / 16777216.0f);
	dith = (a - b) * (1.0f / 128.0f);
	x += dith;
	if (x > 1.0f)
		x = 1.0f;
	if (x < -1.0f)
		x = -1.0f;
	v = (int)lrintf(x * 128.0f);
	if (v > 127)
		v = 127;
	if (v < -128)
		v = -128;
	return (signed char)v;
}

int st_host_src_open(StHostSrc *s, unsigned in_rate)
{
	int err = 0;

	if (!s)
		return 0;
	memset(s, 0, sizeof(*s));
	in_rate = st_host_normalize_rate(in_rate);
	if (!in_rate)
		return 0;
	s->in_rate = in_rate;
	s->ratio = (double)ST_HOST_DMA_RATE / (double)in_rate;
	s->dither = 0xA341316Cu;
	s->state = src_new(SRC_SINC_BEST_QUALITY, 1, &err);
	if (!s->state || err != 0)
		return 0;
	s->opened = 1;
	return 1;
}

void st_host_src_close(StHostSrc *s)
{
	if (!s)
		return;
	if (s->state)
		src_delete((SRC_STATE *)s->state);
	memset(s, 0, sizeof(*s));
}

unsigned st_host_src_in_space(const StHostSrc *s)
{
	if (!s || !s->opened)
		return 0;
	return ST_HOST_SRC_RING - s->in_n;
}

static unsigned process_into_s8(StHostSrc *s, signed char *dst, unsigned n);

static int flush_in_to_pend(StHostSrc *s)
{
	unsigned room, got;

	if (s->pend_n >= ST_HOST_SRC_RING)
		return 0;
	room = ST_HOST_SRC_RING - s->pend_n;
	got = process_into_s8(s, s->pend + s->pend_n, room);
	s->pend_n += got;
	return got > 0;
}

int st_host_src_push_s16(StHostSrc *s, const int16_t *in, unsigned n)
{
	unsigned i;

	if (!s || !s->opened)
		return 0;
	if (!n)
		return 1;
	if (!in)
		return 0;
	while (n) {
		unsigned space = ST_HOST_SRC_RING - s->in_n;
		unsigned chunk;
		if (space == 0) {
			if (!flush_in_to_pend(s))
				return 0;
			continue;
		}
		chunk = n < space ? n : space;
		for (i = 0; i < chunk; i++)
			s->in_ring[s->in_n + i] = (float)in[i] * (1.0f / 32768.0f);
		s->in_n += chunk;
		in += chunk;
		n -= chunk;
	}
	return 1;
}

int st_host_src_finish(StHostSrc *s)
{
	if (!s || !s->opened)
		return 0;
	s->eof = 1;
	return 1;
}

static unsigned process_into_s8(StHostSrc *s, signed char *dst, unsigned n)
{
	SRC_DATA data;
	int err;
	unsigned wrote = 0;

	if (!s || !s->state || !dst || !n)
		return 0;

	while (wrote < n) {
		long want = (long)(n - wrote);
		if (want > (long)ST_HOST_SRC_RING)
			want = (long)ST_HOST_SRC_RING;
		memset(&data, 0, sizeof(data));
		data.data_in = s->in_n ? s->in_ring : NULL;
		data.input_frames = (long)s->in_n;
		data.data_out = s->out_tmp;
		data.output_frames = want;
		data.src_ratio = s->ratio;
		data.end_of_input = s->eof ? 1 : 0;
		err = src_process((SRC_STATE *)s->state, &data);
		if (err != 0)
			return wrote;
		if (data.input_frames_used > 0 && (unsigned)data.input_frames_used <= s->in_n) {
			unsigned used = (unsigned)data.input_frames_used;
			unsigned left = s->in_n - used;
			if (left)
				memmove(s->in_ring, s->in_ring + used, left * sizeof(float));
			s->in_n = left;
		}
		if (data.output_frames_gen > 0) {
			long g, i;
			g = data.output_frames_gen;
			for (i = 0; i < g && wrote < n; i++)
				dst[wrote++] = quant_s8(s, s->out_tmp[i]);
		} else {
			if (!s->eof && s->in_n == 0)
				break;
			if (s->eof && s->in_n == 0 && data.output_frames_gen == 0)
				break;
			if (data.input_frames_used == 0 && data.output_frames_gen == 0)
				break;
		}
	}
	return wrote;
}

unsigned st_host_src_pull_s8(StHostSrc *s, signed char *dst, unsigned n)
{
	unsigned out = 0;

	if (!s || !dst || !n)
		return 0;
	if (s->pend_n) {
		unsigned take = n < s->pend_n ? n : s->pend_n;
		memcpy(dst, s->pend, take);
		if (s->pend_n > take)
			memmove(s->pend, s->pend + take, (s->pend_n - take) * sizeof(s->pend[0]));
		s->pend_n -= take;
		out = take;
	}
	if (out < n)
		out += process_into_s8(s, dst + out, n - out);
	return out;
}

unsigned st_host_src_pull_u8(StHostSrc *s, unsigned char *dst, unsigned n)
{
	signed char tmp[ST_HOST_SRC_RING];
	unsigned done = 0;

	if (!dst)
		return 0;
	while (done < n) {
		unsigned chunk = n - done;
		unsigned got, i;
		if (chunk > ST_HOST_SRC_RING)
			chunk = ST_HOST_SRC_RING;
		got = st_host_src_pull_s8(s, tmp, chunk);
		if (!got)
			break;
		for (i = 0; i < got; i++)
			dst[done + i] = (unsigned char)((int)tmp[i] + 128);
		done += got;
	}
	return done;
}
