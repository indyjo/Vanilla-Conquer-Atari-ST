#ifndef STE_STREAM_PCM_H
#define STE_STREAM_PCM_H

#include "ste_stream_format.h"

class SteStreamPcmFormat final : public SteStreamFormat {
public:
	SteStreamPcmFormat();

	/* Parse AUD header and point at in-MIX payload; returns 0 on failure. */
	int bind_from_aud(unsigned char const* aud, unsigned long aud_bytes);

	void reset() override;

	unsigned long total_output_samples() const override
	{
		return total_output_samples_;
	}
	SteStreamSampleDomain sample_domain() const override
	{
		return sample_domain_;
	}
	unsigned long pull(unsigned char* dst, unsigned long sample_count, unsigned char const lut[256]) override;
	unsigned long skip(unsigned long sample_count) override;

private:
	unsigned char const* raw_ptr_;
	unsigned long raw_left_;
	unsigned long total_output_samples_;
	int pcm_layout_flags_;
	unsigned in_stride_;
	SteStreamSampleDomain sample_domain_;
	int duplicate_2x_;
	int repeat_pending_;
	unsigned char repeat_sample_;

	unsigned long pull_dup2x_(unsigned char* dst, unsigned long sample_count, unsigned char const lut[256]);
	unsigned long skip_dup2x_(unsigned long sample_count);
};

#endif /* STE_STREAM_PCM_H */
