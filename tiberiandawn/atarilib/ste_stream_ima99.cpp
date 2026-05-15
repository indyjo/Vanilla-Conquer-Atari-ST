#include "ste_stream_ima99.h"
#include "st_border_profile.h"

#include <string.h>

SteStreamIma99Format::SteStreamIma99Format() : total_output_samples_(0UL)
{
	reset();
	ws_adpcm68k_init_tables();
}

void SteStreamIma99Format::reset()
{
	memset(&ima_, 0, sizeof(ima_));
	total_output_samples_ = 0UL;
}

void SteStreamIma99Format::stream_init_(unsigned char const* payload, unsigned long payload_len, int channels)
{
	memset(&ima_, 0, sizeof(ima_));
	ima_.pay = payload;
	ima_.pay_len = payload_len;
	ima_.next_hdr = payload;
	ima_.channels = channels;
}

int SteStreamIma99Format::bind_from_aud(unsigned char const* aud, unsigned long aud_bytes)
{
	reset();
	if (aud_bytes < (unsigned long)STE_AUD_HDR_LEN || aud == nullptr) {
		return 0;
	}
	if (aud[11] != STE_AUD_COMP_IMA99) {
		return 0;
	}
	unsigned char const flags = aud[10];
	if ((flags & AUD_FLAG_STEREO) != 0) {
		return 0;
	}

	unsigned long const size_file = ste_aud_read_le32(aud + 2);
	unsigned long const uncomp = ste_aud_read_le32(aud + 6);
	unsigned const aud_stride = (flags & AUD_FLAG_16BIT) ? 2U : 1U;

	if (size_file == 0UL || size_file > STE_AUD99_MAX_COMPRESSED_PAYLOAD || uncomp == 0UL || (uncomp & 1U) != 0UL
	    || uncomp > STE_AUD99_MAX_DECODED_PCM_BYTES) {
		return 0;
	}
	unsigned long pcm_cap = uncomp;
	if (pcm_cap == 0UL || pcm_cap > STE_AUD99_MAX_DECODED_PCM_BYTES) {
		return 0;
	}
	unsigned long const payload_avail =
	    aud_bytes > (unsigned long)STE_AUD_HDR_LEN ? aud_bytes - (unsigned long)STE_AUD_HDR_LEN : 0;
	unsigned long payload_len = size_file;
	if (payload_len > payload_avail) {
		payload_len = payload_avail;
	}
	if (payload_len == 0UL) {
		return 0;
	}
	unsigned long const src_samples = pcm_cap / aud_stride;
	if (src_samples == 0UL) {
		return 0;
	}

	unsigned char const* const payload = aud + STE_AUD_HDR_LEN;
	stream_init_(payload, payload_len, 1);
	total_output_samples_ = src_samples;
	return 1;
}

int SteStreamIma99Format::open_next_frame_()
{
	if (ima_.channels != 1) {
		return 0;
	}
	unsigned char const* const pay_end = ima_.pay + ima_.pay_len;
	if (ima_.next_hdr + 8 > pay_end) {
		return 0;
	}
	unsigned comp = ste_aud_read_le16(ima_.next_hdr);
	unsigned decomp = ste_aud_read_le16(ima_.next_hdr + 2);
	unsigned magic = (unsigned)ste_aud_read_le32(ima_.next_hdr + 4);
	if (magic != STE_AUD99_FRAME_MAGIC || comp == 0 || decomp == 0 || (decomp & 1U) != 0
	    || ima_.next_hdr + 8 + comp > pay_end) {
		return 0;
	}
	unsigned char const* const chunk = ima_.next_hdr + 8;

	unsigned frame_pcm = decomp;
	unsigned const cap = comp * 4U;
	if (frame_pcm > cap) {
		frame_pcm = cap;
	}
	if (frame_pcm == 0 || (frame_pcm & 1U) != 0 || frame_pcm > STE_AUD99_MAX_SINGLE_FRAME_PCM) {
		return 0;
	}
	unsigned const frame_samples = frame_pcm >> 1;
	ima_.next_hdr += 8 + comp;
	ima_.frame_comp_base = chunk;
	ima_.frame_comp_len = comp;
	ima_.frame_comp_off = 0;
	ima_.frame_samples_total = frame_samples;
	ima_.frame_samples_emitted = 0;
	return 1;
}

unsigned SteStreamIma99Format::stream_pull_(signed char* dst, unsigned max_out)
{
	unsigned written = 0;
	while (written < max_out) {
		if (ima_.frame_samples_emitted >= ima_.frame_samples_total) {
			ima_.frame_comp_off = ima_.frame_comp_len;
			if (!open_next_frame_()) {
				break;
			}
		}

		unsigned const need = max_out - written;
		unsigned const rem_samples = ima_.frame_samples_total - ima_.frame_samples_emitted;
		unsigned n = need < rem_samples ? need : rem_samples;
		n &= ~1U;
		if (n == 0) {
			break;
		}

		unsigned const comp_left = ima_.frame_comp_len - ima_.frame_comp_off;
		unsigned char const* const csrc = ima_.frame_comp_base + ima_.frame_comp_off;
		unsigned src_used = 0;
		BORDER_COLOR(0x07F0u);
		unsigned const produced =
		    ws_adpcm68k_decode_mono8(&ima_.ws_mono, csrc, comp_left, dst + written, n, &src_used);
		BORDER_RESTORE();
		ima_.frame_comp_off += src_used;
		ima_.frame_samples_emitted += produced;
		written += produced;
		if (produced == 0) {
			break;
		}
	}
	return written;
}

unsigned long SteStreamIma99Format::pull(signed char* dst, unsigned long sample_count)
{
	if (dst == nullptr || sample_count == 0UL || total_output_samples_ == 0UL) {
		return 0UL;
	}
	return (unsigned long)stream_pull_(dst, (unsigned)sample_count);
}

unsigned long SteStreamIma99Format::skip(unsigned long sample_count)
{
	unsigned long skipped = 0;
	unsigned long left = sample_count;
	while (left > 0UL) {
		unsigned batch = left > (unsigned long)STE_IMA_SKIP_SCRATCH ? (unsigned)STE_IMA_SKIP_SCRATCH : (unsigned)left;
		batch &= ~1U;
		if (batch == 0) {
			break;
		}
		unsigned const got = stream_pull_(skip_scratch_, batch);
		if (got == 0) {
			break;
		}
		skipped += (unsigned long)got;
		left -= (unsigned long)got;
	}
	return skipped;
}
