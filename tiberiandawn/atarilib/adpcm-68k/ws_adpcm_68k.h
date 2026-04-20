#ifndef WS_ADPCM_68K_H
#define WS_ADPCM_68K_H

struct WsAdpcm68kState {
	int predictor;
	int step_index;
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
 * Same inner decode as ws_adpcm68k_decode_mono16 but writes signed 8-bit directly via a
 * 256-entry volume LUT (indexed by (pred >> 8) + 128). The caller must populate the LUT once
 * before the first call via ws_adpcm68k_build_volume_lut(); it's stored in caller memory so
 * concurrent streams at different volumes don't clobber each other. This avoids a 32-bit
 * multiply per sample, which would otherwise trap to __mulsi3 on 68000 (no muls.l) and cause
 * highly variable per-sample cost.
 */
void ws_adpcm68k_build_volume_lut(signed char lut[256], int volume);
void ws_adpcm68k_decode_mono8(
    struct WsAdpcm68kState* st, unsigned char const* src, unsigned src_bytes,
    signed char* dst, unsigned dst_samples, signed char const lut[256]);

#endif
