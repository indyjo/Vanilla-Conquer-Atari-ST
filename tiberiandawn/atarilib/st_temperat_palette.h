/*
 * TEMPERAT.PAL (768 bytes, 6-bit RGB triplets) + helpers to pack the first 16
 * logical colors into Atari STE hardware palette words (same layout as startup).
 *
 * Binary data lives in st_temperat_palette_data.cpp. Keep in sync with
 * tools/palette-opt/playpal.c PLAYPAL[].
 */

#ifndef ST_TEMPERAT_PALETTE_H
#define ST_TEMPERAT_PALETTE_H

#ifdef __cplusplus
extern "C" {
#endif

extern const unsigned char kStTemperatPal768[768];

/* Atari ST STE palette registers (16 words). */
#define ST_HW_PALETTE_REGS ((volatile unsigned short *)0xFF8240u)

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

/* Map one RGB channel from 0-63 (CnC .PAL) to 0-15 (ST STE per-gun range). */
static inline unsigned short St_Pack_ST_HW_From_Rgb6_Channel(unsigned char c6)
{
	unsigned short c = (unsigned short)((((unsigned)c6 & 63U) * 15U + 31U) / 63U);
	return (unsigned short)(((c >> 1) & 0x7) | ((c & 0x1) << 3));
}

static inline unsigned short St_Pack_ST_HW_From_Rgb6(unsigned char r6, unsigned char g6, unsigned char b6)
{
	unsigned short pr = St_Pack_ST_HW_From_Rgb6_Channel(r6);
	unsigned short pg = St_Pack_ST_HW_From_Rgb6_Channel(g6);
	unsigned short pb = St_Pack_ST_HW_From_Rgb6_Channel(b6);
	return (unsigned short)((pr << 8) | (pg << 4) | pb);
}

/* Load pens 0..15 from the first 16 triplets of a 768-byte logical palette. */
static inline void St_HW_Palette_Write_First16_From_Logical_Pal6(volatile unsigned short *regs,
		const unsigned char *pal768)
{
	for (int i = 0; i < 16; i++) {
		unsigned char r = pal768[i * 3 + 0];
		unsigned char g = pal768[i * 3 + 1];
		unsigned char b = pal768[i * 3 + 2];
		regs[i] = St_Pack_ST_HW_From_Rgb6(r, g, b);
	}
}

/* Load pens 0..15 from subset16[pen] indices into a 768-byte logical palette. */
static inline void St_HW_Palette_Write_16_From_Logical_Pal6_Subset(volatile unsigned short *regs,
		const unsigned char *pal768, const unsigned char *subset16)
{
	for (int i = 0; i < 16; i++) {
		const int idx = (int)subset16[i] & 255;
		unsigned char r = pal768[idx * 3 + 0];
		unsigned char g = pal768[idx * 3 + 1];
		unsigned char b = pal768[idx * 3 + 2];
		regs[i] = St_Pack_ST_HW_From_Rgb6(r, g, b);
	}
}

static inline void St_HW_Palette_Write_Temperat_First16(volatile unsigned short *regs)
{
	St_HW_Palette_Write_First16_From_Logical_Pal6(regs, kStTemperatPal768);
}

#endif /* __cplusplus */

#endif /* ST_TEMPERAT_PALETTE_H */
