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
	unsigned long pull(signed char* dst, unsigned long sample_count) override;
	unsigned long skip(unsigned long sample_count) override;

private:
	unsigned char const* raw_ptr_;
	unsigned long raw_left_;
	unsigned long total_output_samples_;
	int pcm_layout_flags_;
	unsigned in_stride_;
};

#endif /* STE_STREAM_PCM_H */
