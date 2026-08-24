#ifndef STE_STREAM_IMA99_H
#define STE_STREAM_IMA99_H

#include "ste_aud_constants.h"
#include "ste_stream_format.h"
#include "ws_adpcm_68k.h"

class SteStreamIma99Format final : public SteStreamFormat {
public:
	SteStreamIma99Format();

	/* Parse AUD header and point at in-MIX payload; returns 0 on failure. */
	int bind_from_aud(unsigned char const* aud, unsigned long aud_bytes);

	void reset() override;

	unsigned long total_output_samples() const override
	{
		return total_output_samples_;
	}
	SteStreamSampleDomain sample_domain() const override
	{
		return STE_STREAM_DOMAIN_S8;
	}
	unsigned long pull(unsigned char* dst, unsigned long sample_count, unsigned char const lut[256]) override;
	unsigned long skip(unsigned long sample_count) override;
	int at_end() const override;

private:
	struct Ima99Core {
		unsigned char const* pay;
		unsigned long pay_len;
		unsigned char const* next_hdr;
		int channels;
		WsAdpcm68kState ws_mono;
		unsigned char const* frame_comp_base;
		unsigned frame_comp_len;
		unsigned frame_comp_off;
		unsigned frame_samples_total;
		unsigned frame_samples_emitted;
	};

	Ima99Core ima_;
	unsigned long total_output_samples_;
	int subsample_2_; /* YM/Covox: emit every other decoded sample (STE never sets). */
	signed char skip_scratch_[STE_IMA_SKIP_SCRATCH];
	signed char sub2_scratch_[STE_IMA_SKIP_SCRATCH];

	void stream_init_(unsigned char const* payload, unsigned long payload_len, int channels);
	int open_next_frame_();
	unsigned stream_pull_(signed char* dst, unsigned max_out);
	unsigned long pull_sub2_(unsigned char* dst, unsigned long sample_count, unsigned char const lut[256]);
	unsigned long skip_sub2_(unsigned long sample_count);
};

#endif /* STE_STREAM_IMA99_H */
