/*
 * ste_stream_format.h - Abstract STE digitized stream (.AUD in memory).
 *
 * PCM and IMA99 implementations are constructed once in Audio_Init and rebound per play
 * via bind_from_aud(). Each stream advertises whether its logical 8-bit output domain is
 * signed or unsigned; the caller supplies a 256-byte lookup table for pull(), indexed in
 * that domain, so conversion and per-voice scaling can be fused by the format driver.
 */
#ifndef STE_STREAM_FORMAT_H
#define STE_STREAM_FORMAT_H

enum SteStreamSampleDomain {
	STE_STREAM_DOMAIN_S8 = 0,
	STE_STREAM_DOMAIN_U8 = 1
};

class SteStreamFormat {
public:
	virtual ~SteStreamFormat() = default;

	/* Total logical mono output samples (declared AUD length; may pad with silence). */
	virtual unsigned long total_output_samples() const = 0;
	virtual SteStreamSampleDomain sample_domain() const = 0;

	/*
	 * Decode up to `sample_count` mono 8-bit samples into `dst`, mapping each logical sample
	 * through `lut[256]`. The stream's `sample_domain()` specifies how the LUT is indexed.
	 * Returns how many samples were written (no zero-fill past that; caller pads if needed).
	 */
	virtual unsigned long pull(unsigned char* dst, unsigned long sample_count, unsigned char const lut[256]) = 0;

	/*
	 * Advance the logical stream by up to `sample_count` output samples without writing PCM.
	 * Returns how many samples were skipped (may be less if the stream ends first).
	 */
	virtual unsigned long skip(unsigned long sample_count) = 0;

	/* Drop current source; instance remains valid for bind_from_aud(). */
	virtual void reset() = 0;

protected:
	SteStreamFormat() = default;
};

#endif /* STE_STREAM_FORMAT_H */
