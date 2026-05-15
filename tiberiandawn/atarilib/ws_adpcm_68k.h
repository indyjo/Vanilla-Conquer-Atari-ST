#ifndef WS_ADPCM_68K_H
#define WS_ADPCM_68K_H

struct WsAdpcm68kState {
	int predictor;
	short step_index;
};

/* Build once: [step_index(0..88)][nibble(0..15)] -> signed predictor delta. */
void ws_adpcm68k_init_tables(void);

/*
 * Decode Westwood IMA (AUD99 / ADPCM_IMA_WS style, shift=3).
 * Consumes src_bytes of packed nibbles (low then high nibble per byte)
 * and emits dst_samples signed 16-bit samples.
 */
void ws_adpcm68k_decode_mono16(
    struct WsAdpcm68kState* st, unsigned char const* src, unsigned src_bytes, short* dst, unsigned dst_samples);

/*
 * Decode up to dst_samples mono signed 8-bit from Westwood IMA packed bytes (full-scale from
 * predictor; no volume). Each output sample is (pred >> 8) clamped to -128..127.
 * dst_samples must be even (whole packed bytes only); otherwise returns 0.
 * Returns samples written (always even, <= dst_samples). *out_src_bytes_used is bytes consumed.
 */
unsigned ws_adpcm68k_decode_mono8(
    struct WsAdpcm68kState* st, unsigned char const* src, unsigned src_bytes,
    signed char* dst, unsigned dst_samples, unsigned* out_src_bytes_used);

#endif
