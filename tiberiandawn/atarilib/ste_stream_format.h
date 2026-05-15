/*
 * ste_stream_format.h - Abstract STE digitized stream (.AUD in memory).
 *
 * PCM and IMA99 implementations are constructed once in Audio_Init and rebound per play
 * via bind_from_aud(). Decoded output is full-scale mono signed 8-bit; the mixer applies
 * per-voice volume when writing to the DMA ring.
 */
#ifndef STE_STREAM_FORMAT_H
#define STE_STREAM_FORMAT_H

class SteStreamFormat {
public:
	virtual ~SteStreamFormat() = default;

	/* Total logical mono output samples (declared AUD length; may pad with silence). */
	virtual unsigned long total_output_samples() const = 0;

	/*
	 * Decode up to `sample_count` mono signed 8-bit samples into `dst`. Returns how many
	 * samples were written (no zero-fill past that; caller pads if needed).
	 */
	virtual unsigned long pull(signed char* dst, unsigned long sample_count) = 0;

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
