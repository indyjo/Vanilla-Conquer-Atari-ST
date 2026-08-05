#include "ste_stream_pcm.h"

#include "audx/ste_stream_source.h"
#include "ste_aud_constants.h"

/* 8-bit mono PCM → volume LUT. */
static void pcm_linear_lut(unsigned char const *src, unsigned n, unsigned char const lut[256], unsigned char *dst)
{
	if (n == 0U) {
		return;
	}
#if defined(__GNUC__) && defined(__m68k__)
	{
		/*
		 * One moveq, then dbra. .w index is enough after moveq.
		 * GCC's C loop reloads lut from the stack every sample (~15% CPU in profile).
		 */
		unsigned char const *s = src;
		unsigned char *d = dst;
		unsigned cnt = n;
		unsigned idx;
		__asm__ __volatile__(
			"moveq #0,%[idx]\n\t"
			"subq.w #1,%[cnt]\n\t"
			"0:\n\t"
			"move.b (%[src])+,%[idx]\n\t"
			"move.b (%[lut],%[idx].w),(%[dst])+\n\t"
			"dbra.w %[cnt],0b\n\t"
			: [src] "+a"(s), [dst] "+a"(d), [cnt] "+d"(cnt), [idx] "=&d"(idx)
			: [lut] "a"(lut)
			: "cc", "memory");
		(void)s;
		(void)d;
	}
#else
	for (unsigned i = 0; i < n; ++i) {
		dst[i] = lut[src[i]];
	}
#endif
}

SteStreamPcmFormat::SteStreamPcmFormat()
    : source_(0),
      bytes_left_(0UL),
      total_output_samples_(0UL),
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
	if ((flags & AUD_FLAG_16BIT) != 0) {
		return 0;
	}

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
	if (prime_len == 0UL) {
		return 0;
	}

	source_ = source;
	source_->rewind();
	bytes_left_ = prime_len;
	total_output_samples_ = duplicate_2x_ ? (pcm_cap * 2UL) : pcm_cap;
	return 1;
}

unsigned long SteStreamPcmFormat::fill_scratch_(unsigned long nbytes)
{
	if (!source_ || nbytes == 0UL || nbytes > sizeof(scratch_)) {
		return 0UL;
	}
	unsigned long const got = source_->read(scratch_, nbytes);
	if (got == 0UL && source_->at_end()) {
		bytes_left_ = 0UL;
		repeat_pending_ = 0;
	}
	return got;
}

unsigned long SteStreamPcmFormat::pull_dup2x_(unsigned char *dst, unsigned long sample_count,
    unsigned char const lut[256])
{
	unsigned long written = 0UL;

	if (repeat_pending_ && written < sample_count) {
		dst[written++] = lut[repeat_sample_];
		repeat_pending_ = 0;
	}

	while (written < sample_count && bytes_left_ >= 1UL) {
		unsigned long const pair_cap = (sample_count - written) >> 1;
		if (pair_cap == 0UL) {
			if (fill_scratch_(1UL) != 1UL) {
				break;
			}
			unsigned char const s = scratch_[0];
			dst[written++] = lut[s];
			bytes_left_ -= 1UL;
			repeat_sample_ = s;
			repeat_pending_ = 1;
			break;
		}

		unsigned long pairs = pair_cap;
		if (pairs > bytes_left_) {
			pairs = bytes_left_;
		}
		unsigned long const batch_pairs = pairs > 256UL ? 256UL : pairs;
		unsigned long const got = fill_scratch_(batch_pairs);
		if (got == 0UL) {
			break;
		}
		unsigned char *d = dst + written;
		for (unsigned long i = 0; i < got; ++i) {
			unsigned char const out = lut[scratch_[i]];
			*d++ = out;
			*d++ = out;
		}
		bytes_left_ -= got;
		written += got << 1;
	}

	return written;
}

unsigned long SteStreamPcmFormat::pull(unsigned char *dst, unsigned long sample_count, unsigned char const lut[256])
{
	if (!source_ || sample_count == 0UL || dst == nullptr || lut == nullptr) {
		return 0UL;
	}
	if (duplicate_2x_) {
		return pull_dup2x_(dst, sample_count, lut);
	}
	unsigned long written = 0UL;
	while (written < sample_count && bytes_left_ >= 1UL) {
		unsigned long n = sample_count - written;
		if (n > bytes_left_) {
			n = bytes_left_;
		}
		if (n > sizeof(scratch_)) {
			n = sizeof(scratch_);
		}
		unsigned long const got = fill_scratch_(n);
		if (got == 0UL) {
			break;
		}
		pcm_linear_lut(scratch_, (unsigned)got, lut, dst + written);
		bytes_left_ -= got;
		written += got;
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

	while (skipped < sample_count && bytes_left_ >= 1UL) {
		unsigned long const pair_cap = (sample_count - skipped) >> 1;
		if (pair_cap == 0UL) {
			if (source_->skip(1UL) != 1UL) {
				if (source_->at_end()) {
					bytes_left_ = 0UL;
					repeat_pending_ = 0;
				}
				break;
			}
			bytes_left_ -= 1UL;
			repeat_pending_ = 1;
			repeat_sample_ = 0;
			++skipped;
			break;
		}
		unsigned long pairs = pair_cap;
		if (pairs > bytes_left_) {
			pairs = bytes_left_;
		}
		if (source_->skip(pairs) != pairs) {
			if (source_->at_end()) {
				bytes_left_ = 0UL;
				repeat_pending_ = 0;
			}
			break;
		}
		bytes_left_ -= pairs;
		skipped += pairs << 1;
	}
	return skipped;
}

unsigned long SteStreamPcmFormat::skip(unsigned long sample_count)
{
	if (!source_ || sample_count == 0UL) {
		return 0UL;
	}
	if (duplicate_2x_) {
		return skip_dup2x_(sample_count);
	}
	unsigned long skip_amt = sample_count;
	if (skip_amt > bytes_left_) {
		skip_amt = bytes_left_;
	}
	if (source_->skip(skip_amt) != skip_amt) {
		if (source_->at_end()) {
			bytes_left_ = 0UL;
		}
		return 0UL;
	}
	bytes_left_ -= skip_amt;
	return skip_amt;
}

int SteStreamPcmFormat::at_end() const
{
	return !source_ || (bytes_left_ == 0UL && !repeat_pending_);
}
