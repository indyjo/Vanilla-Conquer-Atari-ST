/*
 * st_hw_probe.h — Cookie-jar machine probes (_MCH / _SND).
 */

#ifndef ST_HW_PROBE_H
#define ST_HW_PROBE_H

#ifdef __cplusplus
extern "C" {
#endif

/* _MCH high word, or -1 when the cookie jar has no _MCH entry. */
int ST_Hw_Machine_Major(void);

/* _MCH high word == 1 (STE / Mega STE / ST Book). */
int ST_Hw_Is_Ste_Class(void);

/* _MCH high word == 3 (Falcon030 and compatibles). */
int ST_Hw_Is_Falcon_Class(void);

/*
 * _MCH high word 1 or 2: STE and TT share the same 8-bit DMA sound chip and the
 * LMC1992 mixer at $FF8900..$FF8922. A Falcon has DMA sound too but reaches it
 * through Devconnect and a codec, so it is deliberately not in this class.
 */
int ST_Hw_Is_Ste_Sound_Class(void);

/*
 * 8-bit DMA sound available: not plain ST (_MCH hw 0), and _SND bit 1 when
 * _SND is present (Falcon/TT/STE). Without _SND, STE class only.
 */
int ST_Hw_Dma_Audio_Available(void);

/*
 * True when the address lies in ST-RAM, i.e. the memory the shifter, the
 * BLiTTER and DMA audio can reach.
 */
int ST_Hw_Is_St_Ram(const void *addr);

#ifdef __cplusplus
}
#endif

#endif /* ST_HW_PROBE_H */
