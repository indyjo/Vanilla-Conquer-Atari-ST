#ifndef STE_STREAM_PCM_H
#define STE_STREAM_PCM_H

#include "ste_stream_format.h"

class SteStreamSource;

class SteStreamPcmFormat final : public SteStreamFormat {
public:
	SteStreamPcmFormat();

	/*
	 * Bind decoded header fields + payload source (not owned).
	 * compression must be STE_AUD_COMP_PCM; 8-bit mono only.
	 */
	int bind_source(unsigned short rate, unsigned char flags, unsigned char compression, unsigned long size,
	    unsigned long uncomp, SteStreamSource *source);

	void reset() override;

	unsigned long total_output_samples() const override
	{
		return total_output_samples_;
	}
	SteStreamSampleDomain sample_domain() const override
	{
		return STE_STREAM_DOMAIN_U8;
	}
	unsigned long pull(unsigned char *dst, unsigned long sample_count, unsigned char const lut[256]) override;
	unsigned long skip(unsigned long sample_count) override;
	int at_end() const override;

private:
	SteStreamSource *source_;
	unsigned long bytes_left_;
	unsigned long total_output_samples_;
	int duplicate_2x_;
	int repeat_pending_;
	unsigned char repeat_sample_;
	unsigned char scratch_[1024];

	unsigned long pull_dup2x_(unsigned char *dst, unsigned long sample_count, unsigned char const lut[256]);
	unsigned long skip_dup2x_(unsigned long sample_count);
	/* Read into scratch_; returns bytes obtained (0 = stall or EOF). */
	unsigned long fill_scratch_(unsigned long nbytes);
};

#endif /* STE_STREAM_PCM_H */
