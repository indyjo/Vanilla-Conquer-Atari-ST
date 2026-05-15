#include "ste_stream_pcm.h"

#include "ste_aud_constants.h"

#include <string.h>

static void pcm_linear_s8(unsigned char const* src, int flags, unsigned n, signed char* dst)
{
	int const sixteen = (flags & AUD_FLAG_16BIT) != 0;
	if (sixteen) {
		for (unsigned i = 0; i < n; ++i) {
			dst[i] = (signed char)src[i * 2 + 1];
		}
	} else {
		for (unsigned i = 0; i < n; ++i) {
			dst[i] = (signed char)((int)src[i] - 128);
		}
	}
}

SteStreamPcmFormat::SteStreamPcmFormat()
    : raw_ptr_(0),
      raw_left_(0UL),
      total_output_samples_(0UL),
      pcm_layout_flags_(0),
      in_stride_(0U)
{
}

void SteStreamPcmFormat::reset()
{
	raw_ptr_ = 0;
	raw_left_ = 0UL;
	total_output_samples_ = 0UL;
	pcm_layout_flags_ = 0;
	in_stride_ = 0U;
}

int SteStreamPcmFormat::bind_from_aud(unsigned char const* aud, unsigned long aud_bytes)
{
	reset();
	if (aud_bytes < (unsigned long)STE_AUD_HDR_LEN || aud == nullptr) {
		return 0;
	}
	if (aud[11] != STE_AUD_COMP_PCM) {
		return 0;
	}
	unsigned char const flags = aud[10];
	if ((flags & AUD_FLAG_STEREO) != 0) {
		return 0;
	}

	unsigned long const uncomp = ste_aud_read_le32(aud + 6);
	unsigned const aud_stride = (flags & AUD_FLAG_16BIT) ? 2U : 1U;
	pcm_layout_flags_ = (int)flags;
	in_stride_ = aud_stride;

	unsigned long pcm_cap = uncomp;
	if (pcm_cap == 0UL && aud_bytes > (unsigned long)STE_AUD_HDR_LEN) {
		pcm_cap = aud_bytes - (unsigned long)STE_AUD_HDR_LEN;
	}
	if (pcm_cap == 0UL || pcm_cap > STE_AUD99_MAX_DECODED_PCM_BYTES) {
		return 0;
	}
	unsigned long const payload_avail =
	    aud_bytes > (unsigned long)STE_AUD_HDR_LEN ? aud_bytes - (unsigned long)STE_AUD_HDR_LEN : 0;
	unsigned long const prime_len = payload_avail < pcm_cap ? payload_avail : pcm_cap;
	unsigned long const src_samples = pcm_cap / aud_stride;
	if (src_samples == 0UL) {
		return 0;
	}

	raw_ptr_ = aud + STE_AUD_HDR_LEN;
	raw_left_ = prime_len;
	total_output_samples_ = src_samples;
	return 1;
}

unsigned long SteStreamPcmFormat::pull(signed char* dst, unsigned long sample_count)
{
	if (in_stride_ == 0U || sample_count == 0UL || dst == nullptr) {
		return 0UL;
	}
	unsigned long const avail_out = raw_left_ / (unsigned long)in_stride_;
	unsigned long const n = sample_count < avail_out ? sample_count : avail_out;
	if (n > 0UL) {
		pcm_linear_s8(raw_ptr_, pcm_layout_flags_, (unsigned)n, dst);
		raw_ptr_ += n * (unsigned long)in_stride_;
		raw_left_ -= n * (unsigned long)in_stride_;
	}
	return n;
}

unsigned long SteStreamPcmFormat::skip(unsigned long sample_count)
{
	if (in_stride_ == 0U || sample_count == 0UL) {
		return 0UL;
	}
	unsigned long skip_amt = sample_count;
	unsigned long const max_skip = raw_left_ / (unsigned long)in_stride_;
	if (skip_amt > max_skip) {
		skip_amt = max_skip;
	}
	unsigned long const adv = skip_amt * (unsigned long)in_stride_;
	raw_ptr_ += adv;
	raw_left_ -= adv;
	return skip_amt;
}
