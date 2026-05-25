#include "ste_stream_pcm.h"

#include "ste_aud_constants.h"

static void pcm_linear_lut(unsigned char const* src, int flags, unsigned n, unsigned char const lut[256], unsigned char* dst)
{
	int const sixteen = (flags & AUD_FLAG_16BIT) != 0;
	if (sixteen) {
		for (unsigned i = 0; i < n; ++i) {
			dst[i] = lut[src[i * 2 + 1]];
		}
	} else {
		for (unsigned i = 0; i < n; ++i) {
			dst[i] = lut[src[i]];
		}
	}
}

static unsigned char pcm_native_byte(unsigned char const* src, int flags)
{
	if ((flags & AUD_FLAG_16BIT) != 0) {
		return src[1];
	}
	return src[0];
}

SteStreamPcmFormat::SteStreamPcmFormat()
    : raw_ptr_(0),
      raw_left_(0UL),
      total_output_samples_(0UL),
      pcm_layout_flags_(0),
      in_stride_(0U),
      sample_domain_(STE_STREAM_DOMAIN_U8),
      duplicate_2x_(0),
      repeat_pending_(0),
      repeat_sample_(0)
{
}

void SteStreamPcmFormat::reset()
{
	raw_ptr_ = 0;
	raw_left_ = 0UL;
	total_output_samples_ = 0UL;
	pcm_layout_flags_ = 0;
	in_stride_ = 0U;
	sample_domain_ = STE_STREAM_DOMAIN_U8;
	duplicate_2x_ = 0;
	repeat_pending_ = 0;
	repeat_sample_ = 0;
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
	sample_domain_ = (flags & AUD_FLAG_16BIT) != 0 ? STE_STREAM_DOMAIN_S8 : STE_STREAM_DOMAIN_U8;
	duplicate_2x_ = (flags & STE_AUD_FLAG_DUP2X) != 0;

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
	total_output_samples_ = duplicate_2x_ ? (src_samples * 2UL) : src_samples;
	return 1;
}

unsigned long SteStreamPcmFormat::pull_dup2x_(unsigned char* dst, unsigned long sample_count, unsigned char const lut[256])
{
	unsigned long written = 0UL;
	unsigned char const* src = raw_ptr_;
	unsigned long src_left = raw_left_;

	if (repeat_pending_ && written < sample_count) {
		dst[written++] = lut[repeat_sample_];
		repeat_pending_ = 0;
	}

	if (written < sample_count) {
		unsigned long const pair_cap = (sample_count - written) >> 1;
		if (in_stride_ == 1U) {
			unsigned long pairs = pair_cap;
			if (pairs > src_left) {
				pairs = src_left;
			}
			unsigned char* d = dst + written;
			for (unsigned long i = 0; i < pairs; ++i) {
				unsigned char const out = lut[*src++];
				*d++ = out;
				*d++ = out;
			}
			src_left -= pairs;
			written += pairs << 1;
		} else {
			unsigned long pairs = pair_cap;
			unsigned long const avail_pairs = src_left >> 1;
			if (pairs > avail_pairs) {
				pairs = avail_pairs;
			}
			unsigned char* d = dst + written;
			for (unsigned long i = 0; i < pairs; ++i) {
				unsigned char const out = lut[src[1]];
				src += 2;
				*d++ = out;
				*d++ = out;
			}
			src_left -= pairs << 1;
			written += pairs << 1;
		}
	}

	if (written < sample_count && src_left >= (unsigned long)in_stride_) {
		unsigned char const s = pcm_native_byte(src, pcm_layout_flags_);
		dst[written++] = lut[s];
		src += (unsigned long)in_stride_;
		src_left -= (unsigned long)in_stride_;
		repeat_sample_ = s;
		repeat_pending_ = 1;
	}

	raw_ptr_ = src;
	raw_left_ = src_left;
	return written;
}

unsigned long SteStreamPcmFormat::pull(unsigned char* dst, unsigned long sample_count, unsigned char const lut[256])
{
	if (in_stride_ == 0U || sample_count == 0UL || dst == nullptr || lut == nullptr) {
		return 0UL;
	}
	if (duplicate_2x_) {
		return pull_dup2x_(dst, sample_count, lut);
	}
	unsigned long const avail_out = raw_left_ / (unsigned long)in_stride_;
	unsigned long const n = sample_count < avail_out ? sample_count : avail_out;
	if (n > 0UL) {
		pcm_linear_lut(raw_ptr_, pcm_layout_flags_, (unsigned)n, lut, dst);
		raw_ptr_ += n * (unsigned long)in_stride_;
		raw_left_ -= n * (unsigned long)in_stride_;
	}
	return n;
}

unsigned long SteStreamPcmFormat::skip_dup2x_(unsigned long sample_count)
{
	unsigned long skipped = 0UL;
	unsigned char const* src = raw_ptr_;
	unsigned long src_left = raw_left_;

	if (repeat_pending_ && skipped < sample_count) {
		repeat_pending_ = 0;
		++skipped;
	}

	if (skipped < sample_count) {
		unsigned long const pair_cap = (sample_count - skipped) >> 1;
		if (in_stride_ == 1U) {
			unsigned long pairs = pair_cap;
			if (pairs > src_left) {
				pairs = src_left;
			}
			src += pairs;
			src_left -= pairs;
			skipped += pairs << 1;
		} else {
			unsigned long pairs = pair_cap;
			unsigned long const avail_pairs = src_left >> 1;
			if (pairs > avail_pairs) {
				pairs = avail_pairs;
			}
			src += pairs << 1;
			src_left -= pairs << 1;
			skipped += pairs << 1;
		}
	}

	if (skipped < sample_count && src_left >= (unsigned long)in_stride_) {
		repeat_sample_ = pcm_native_byte(src, pcm_layout_flags_);
		src += (unsigned long)in_stride_;
		src_left -= (unsigned long)in_stride_;
		repeat_pending_ = 1;
		++skipped;
	}

	raw_ptr_ = src;
	raw_left_ = src_left;
	return skipped;
}

unsigned long SteStreamPcmFormat::skip(unsigned long sample_count)
{
	if (in_stride_ == 0U || sample_count == 0UL) {
		return 0UL;
	}
	if (duplicate_2x_) {
		return skip_dup2x_(sample_count);
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
