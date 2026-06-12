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
 * 8-bit DMA sound available: not plain ST (_MCH hw 0), and _SND bit 1 when
 * _SND is present (Falcon/TT/STE). Without _SND, STE class only.
 */
int ST_Hw_Dma_Audio_Available(void);

#ifdef __cplusplus
}
#endif

#endif /* ST_HW_PROBE_H */
