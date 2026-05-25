/*
 * ste_aud_constants.h - Shared .AUD / AUD99 limits for STE streaming (used by audio_ste and format drivers).
 */
#ifndef STE_AUD_CONSTANTS_H
#define STE_AUD_CONSTANTS_H

/* Single looping DMA buffer in ST-RAM (~41 ms @ 25033 Hz). */
enum { STE_DMA_RING_SAMPLES = 1024 };
/* Simultaneous digitized voices (mixer CPU budget). */
enum { STE_MIX_VOICES = 2 };
/* Per-voice decode scratch (also used for dual-voice mix); keep off stack in VBL. */
enum { STE_AUDIO_PULL_BLOCK = 512 };
/* IMA skip() scratch; must be even for decode. */
enum { STE_IMA_SKIP_SCRATCH = 256 };

enum { STE_AUD_HDR_LEN = 12 };
enum { STE_AUD99_FRAME_MAGIC = 0x0000DEAFUL };
enum { STE_AUD_COMP_PCM = 0, STE_AUD_COMP_IMA99 = 99 };
#define AUD_FLAG_STEREO 1
#define AUD_FLAG_16BIT 2
/* Private in-memory hint: duplicate logical PCM samples on pull() after Sample_Make_PCM(). */
#define STE_AUD_FLAG_DUP2X 4
/* One-shot limits (large IMA scores / long voice). Tune down on very small RAM systems. */
enum { STE_AUD99_MAX_COMPRESSED_PAYLOAD = 2UL * 1024UL * 1024UL }; /* bytes after 12-byte AUD header */
enum { STE_AUD99_MAX_DECODED_PCM_BYTES = 8UL * 1024UL * 1024UL };  /* 16-bit PCM from AUD */
enum { STE_AUD99_MAX_SINGLE_FRAME_PCM = 262144u }; /* max declared decomp bytes per AUD99 frame (sanity) */

#ifdef __cplusplus
inline unsigned short ste_aud_read_le16(unsigned char const* p)
{
	return (unsigned short)((unsigned short)p[0] | ((unsigned short)p[1] << 8));
}
inline unsigned long ste_aud_read_le32(unsigned char const* p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}
#endif

#endif /* STE_AUD_CONSTANTS_H */
