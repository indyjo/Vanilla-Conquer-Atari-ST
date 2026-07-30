#include "ste_stream_pcm.h"

#include "audx/ste_stream_source.h"
#include "ste_aud_constants.h"

#include <string.h>

static void pcm_linear_lut(unsigned char const *src, int flags, unsigned n, unsigned char const lut[256],
    unsigned char *dst)
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

static unsigned char pcm_native_byte(unsigned char const *src, int flags)
{
	if ((flags & AUD_FLAG_16BIT) != 0) {
		return src[1];
	}
	return src[0];
}

SteStreamPcmFormat::SteStreamPcmFormat()
    : source_(0),
      bytes_left_(0UL),
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
	source_ = 0;
	bytes_left_ = 0UL;
	total_output_samples_ = 0UL;
	pcm_layout_flags_ = 0;
	in_stride_ = 0U;
	sample_domain_ = STE_STREAM_DOMAIN_U8;
	duplicate_2x_ = 0;
	repeat_pending_ = 0;
	repeat_sample_ = 0;
}

int SteStreamPcmFormat::bind_source(unsigned short, unsigned char flags, unsigned char compression,
    unsigned long size, unsigned long uncomp, SteStreamSource *source)
{
	reset();
	if (!source || compression != STE_AUD_COMP_PCM) {
		return 0;
	}
	if ((flags & AUD_FLAG_STEREO) != 0) {
		return 0;
	}

	unsigned const aud_stride = (flags & AUD_FLAG_16BIT) ? 2U : 1U;
	pcm_layout_flags_ = (int)flags;
	in_stride_ = aud_stride;
	sample_domain_ = (flags & AUD_FLAG_16BIT) != 0 ? STE_STREAM_DOMAIN_S8 : STE_STREAM_DOMAIN_U8;
	duplicate_2x_ = ((flags & STE_AUD_FLAG_DUP2X) != 0) && g_ste_pcm_dup2x;

	unsigned long pcm_cap = uncomp;
	if (pcm_cap == 0UL) {
		pcm_cap = size;
	}
	if (pcm_cap == 0UL || pcm_cap > STE_AUD99_MAX_DECODED_PCM_BYTES) {
		return 0;
	}
	unsigned long const src_len = source->size();
	unsigned long const prime_len = src_len < pcm_cap ? src_len : pcm_cap;
	unsigned long const src_samples = pcm_cap / aud_stride;
	if (src_samples == 0UL) {
		return 0;
	}

	source_ = source;
	source_->rewind();
	bytes_left_ = prime_len;
	total_output_samples_ = duplicate_2x_ ? (src_samples * 2UL) : src_samples;
	return 1;
}

int SteStreamPcmFormat::bind_from_aud(unsigned char const *aud, unsigned long aud_bytes)
{
	/*
	 * Legacy path: caller keeps aud alive; we require an external memory source
	 * bound by Play_Sample. This overload synthesizes header parse only when
	 * aud points at a contiguous classic AUD blob — Play_Sample uses bind_source.
	 */
	reset();
	if (aud_bytes < (unsigned long)STE_AUD_HDR_LEN || aud == nullptr) {
		return 0;
	}
	if (aud[11] != STE_AUD_COMP_PCM) {
		return 0;
	}
	unsigned char const flags = aud[10];
	unsigned long const size = ste_aud_read_le32(aud + 2);
	unsigned long const uncomp = ste_aud_read_le32(aud + 6);
	(void)size;
	(void)uncomp;
	(void)flags;
	/* Prefer bind_source from Play_Sample; keep stub returning 0 without source. */
	return 0;
}

int SteStreamPcmFormat::fill_scratch_(unsigned long nbytes)
{
	if (!source_ || nbytes == 0UL || nbytes > sizeof(scratch_)) {
		return 0;
	}
	unsigned long const got = source_->read(scratch_, nbytes);
	return got == nbytes ? 1 : 0;
}

unsigned long SteStreamPcmFormat::pull_dup2x_(unsigned char *dst, unsigned long sample_count,
    unsigned char const lut[256])
{
	unsigned long written = 0UL;

	if (repeat_pending_ && written < sample_count) {
		dst[written++] = lut[repeat_sample_];
		repeat_pending_ = 0;
	}

	while (written < sample_count && bytes_left_ >= (unsigned long)in_stride_) {
		unsigned long const pair_cap = (sample_count - written) >> 1;
		if (pair_cap == 0UL) {
			if (!fill_scratch_((unsigned long)in_stride_)) {
				break;
			}
			unsigned char const s = pcm_native_byte(scratch_, pcm_layout_flags_);
			dst[written++] = lut[s];
			bytes_left_ -= (unsigned long)in_stride_;
			repeat_sample_ = s;
			repeat_pending_ = 1;
			break;
		}

		unsigned long pairs = pair_cap;
		unsigned long const max_pairs = bytes_left_ / (unsigned long)in_stride_;
		if (pairs > max_pairs) {
			pairs = max_pairs;
		}
		unsigned long const batch_pairs = pairs > 256UL ? 256UL : pairs;
		unsigned long const nbytes = batch_pairs * (unsigned long)in_stride_;
		if (!fill_scratch_(nbytes)) {
			break;
		}
		unsigned char *d = dst + written;
		if (in_stride_ == 1U) {
			for (unsigned long i = 0; i < batch_pairs; ++i) {
				unsigned char const out = lut[scratch_[i]];
				*d++ = out;
				*d++ = out;
			}
		} else {
			for (unsigned long i = 0; i < batch_pairs; ++i) {
				unsigned char const out = lut[scratch_[i * 2 + 1]];
				*d++ = out;
				*d++ = out;
			}
		}
		bytes_left_ -= nbytes;
		written += batch_pairs << 1;
	}

	return written;
}

unsigned long SteStreamPcmFormat::pull(unsigned char *dst, unsigned long sample_count, unsigned char const lut[256])
{
	if (!source_ || in_stride_ == 0U || sample_count == 0UL || dst == nullptr || lut == nullptr) {
		return 0UL;
	}
	if (duplicate_2x_) {
		return pull_dup2x_(dst, sample_count, lut);
	}
	unsigned long written = 0UL;
	while (written < sample_count && bytes_left_ >= (unsigned long)in_stride_) {
		unsigned long n = sample_count - written;
		unsigned long const avail = bytes_left_ / (unsigned long)in_stride_;
		if (n > avail) {
			n = avail;
		}
		unsigned long const max_batch = sizeof(scratch_) / (unsigned long)in_stride_;
		if (n > max_batch) {
			n = max_batch;
		}
		unsigned long const nbytes = n * (unsigned long)in_stride_;
		if (!fill_scratch_(nbytes)) {
			break;
		}
		pcm_linear_lut(scratch_, pcm_layout_flags_, (unsigned)n, lut, dst + written);
		bytes_left_ -= nbytes;
		written += n;
	}
	return written;
}

unsigned long SteStreamPcmFormat::skip_dup2x_(unsigned long sample_count)
{
	unsigned long skipped = 0UL;

	if (repeat_pending_ && skipped < sample_count) {
		repeat_pending_ = 0;
		++skipped;
	}

	while (skipped < sample_count && bytes_left_ >= (unsigned long)in_stride_) {
		unsigned long const pair_cap = (sample_count - skipped) >> 1;
		if (pair_cap == 0UL) {
			if (source_->skip((unsigned long)in_stride_) != (unsigned long)in_stride_) {
				break;
			}
			bytes_left_ -= (unsigned long)in_stride_;
			repeat_pending_ = 1;
			repeat_sample_ = 0;
			++skipped;
			break;
		}
		unsigned long pairs = pair_cap;
		unsigned long const max_pairs = bytes_left_ / (unsigned long)in_stride_;
		if (pairs > max_pairs) {
			pairs = max_pairs;
		}
		unsigned long const nbytes = pairs * (unsigned long)in_stride_;
		if (source_->skip(nbytes) != nbytes) {
			break;
		}
		bytes_left_ -= nbytes;
		skipped += pairs << 1;
	}
	return skipped;
}

unsigned long SteStreamPcmFormat::skip(unsigned long sample_count)
{
	if (!source_ || in_stride_ == 0U || sample_count == 0UL) {
		return 0UL;
	}
	if (duplicate_2x_) {
		return skip_dup2x_(sample_count);
	}
	unsigned long skip_amt = sample_count;
	unsigned long const max_skip = bytes_left_ / (unsigned long)in_stride_;
	if (skip_amt > max_skip) {
		skip_amt = max_skip;
	}
	unsigned long const adv = skip_amt * (unsigned long)in_stride_;
	if (source_->skip(adv) != adv) {
		return 0UL;
	}
	bytes_left_ -= adv;
	return skip_amt;
}
