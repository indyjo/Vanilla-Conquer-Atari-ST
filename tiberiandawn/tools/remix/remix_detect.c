#include "remix_detect.h"

#include "remix.h"
#include "remix_aud.h"
#include "remix_detect.h"
#include "remix_st16.h"
#include "remix_shpx.h"

#include <stdio.h>
#include <string.h>

static uint16_t read_le16(const unsigned char *p)
{
	return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void remix_format_aud_type(char *type_out, size_t type_out_len, const unsigned char *hdr, const char *codec)
{
	unsigned short rate;
	unsigned char flags;
	int bps;
	char ms;

	if (!type_out || type_out_len == 0 || !hdr || !codec)
		return;
	rate = read_le16(hdr);
	flags = hdr[10];
	bps = (flags & REMIX_AUD_FLAG_16BIT) ? 16 : 8;
	ms = (flags & REMIX_AUD_FLAG_STEREO) ? 'S' : 'M';
	snprintf(type_out, type_out_len, "%s %u/%d/%c", codec, (unsigned)rate, bps, ms);
}

void remix_format_aud_pcm_target(char *type_out, size_t type_out_len)
{
	if (!type_out || type_out_len == 0)
		return;
	snprintf(type_out, type_out_len, "aud_pcm %u/8/M", (unsigned)REMIX_TARGET_RATE);
}

unsigned short remix_aud_normalize_rate(unsigned short rate)
{
	if (rate > 20000u && rate < 24000u)
		return 22050u;
	return rate;
}

static int aud_rate_resampleable(unsigned short rate)
{
	unsigned factor;

	rate = remix_aud_normalize_rate(rate);
	if (rate < (unsigned short)REMIX_TARGET_RATE)
		return 0;
	if (rate == (unsigned short)REMIX_TARGET_RATE)
		return 1;
	if (rate % (unsigned short)REMIX_TARGET_RATE != 0)
		return 0;
	factor = rate / (unsigned short)REMIX_TARGET_RATE;
	return factor > 0;
}

static int aud_ima99_first_frame_ok(const unsigned char *data, size_t probe_len, uint32_t payload_len)
{
	unsigned short comp;
	unsigned short decomp;
	uint32_t magic;

	if (payload_len < 8u)
		return 0;
	if (probe_len < REMIX_AUD_HDR_LEN + 8u)
		return 1;
	comp = read_le16(data + REMIX_AUD_HDR_LEN);
	decomp = read_le16(data + REMIX_AUD_HDR_LEN + 2);
	magic = read_le32(data + REMIX_AUD_HDR_LEN + 4);
	if (magic != REMIX_AUD99_FRAME_MAGIC || comp == 0 || decomp == 0 || (decomp & 1u) != 0)
		return 0;
	if ((uint32_t)comp + 8u > payload_len)
		return 0;
	return 1;
}

int remix_looks_like_aud(
    const unsigned char *data, size_t probe_len, uint32_t file_size, char *type_out, size_t type_out_len)
{
	uint32_t comp_size;
	uint32_t uncomp;
	unsigned char compression;
	uint32_t payload_avail;

	if (probe_len < (size_t)REMIX_AUD_HDR_LEN)
		return 0;

	compression = data[11];
	comp_size = read_le32(data + 2);
	uncomp = read_le32(data + 6);

	if (file_size > (uint32_t)REMIX_AUD_HDR_LEN)
		payload_avail = file_size - (uint32_t)REMIX_AUD_HDR_LEN;
	else
		payload_avail = 0;

	if (comp_size == 0)
		return 0;
	if (payload_avail != 0 && comp_size != payload_avail)
		return 0;
	if (comp_size > REMIX_AUD99_MAX_COMPRESSED_PAYLOAD)
		return 0;
	if (uncomp == 0 || uncomp > REMIX_AUD99_MAX_DECODED_PCM_BYTES)
		return 0;

	if (compression == REMIX_AUD_COMP_IMA99) {
		if (!aud_ima99_first_frame_ok(data, probe_len, comp_size))
			return 0;
		remix_format_aud_type(type_out, type_out_len, data, "aud99");
		return 1;
	}
	if (compression == REMIX_AUD_COMP_WESTWOOD) {
		remix_format_aud_type(type_out, type_out_len, data, "aud_westwood");
		return 1;
	}
	if (compression == REMIX_AUD_COMP_PCM) {
		if (comp_size != uncomp)
			return 0;
		remix_format_aud_type(type_out, type_out_len, data, "aud_pcm");
		return 1;
	}

	return 0;
}

const char *remix_aud_fail_hint(const unsigned char *hdr, size_t hdr_len, uint32_t file_size)
{
	unsigned short rate;
	uint32_t comp_size;
	uint32_t payload_avail;

	if (hdr_len < (size_t)REMIX_AUD_HDR_LEN)
		return "decode";
	rate = read_le16(hdr);
	comp_size = read_le32(hdr + 2);
	if (file_size > (uint32_t)REMIX_AUD_HDR_LEN)
		payload_avail = file_size - (uint32_t)REMIX_AUD_HDR_LEN;
	else
		payload_avail = 0;
	if (payload_avail != 0 && comp_size != payload_avail)
		return "size";
	if (!aud_rate_resampleable(rate))
		return "rate";
	return "decode";
}

static int looks_like_voc(const unsigned char *data, size_t len, char *type_out, size_t type_out_len)
{
	if (len >= 19 && memcmp(data, "Creative Voice File", 19) == 0) {
		snprintf(type_out, type_out_len, "voc");
		return 1;
	}
	return 0;
}

static int looks_like_vqa(const unsigned char *data, size_t len, char *type_out, size_t type_out_len)
{
	if (len < 4 || data[0] != 'F' || data[1] != 'O' || data[2] != 'R' || data[3] != 'M')
		return 0;
	if (len >= 12 && data[8] == 'S' && data[9] == 'T' && data[10] == 'V' && data[11] == 'Q') {
		snprintf(type_out, type_out_len, "stv");
		return 1;
	}
	snprintf(type_out, type_out_len, "vqa");
	return 1;
}

static int looks_like_pcx(const unsigned char *data, size_t len, char *type_out, size_t type_out_len)
{
	if (len >= 2 && data[0] == 0x0A && (data[1] == 0 || data[1] == 5)) {
		snprintf(type_out, type_out_len, "pcx");
		return 1;
	}
	return 0;
}

static int looks_like_pal(uint32_t file_size, char *type_out, size_t type_out_len)
{
	if (file_size == 768 || file_size == 768 + 4) {
		snprintf(type_out, type_out_len, "pal");
		return 1;
	}
	return 0;
}

static int looks_like_ini(const unsigned char *data, size_t len, char *type_out, size_t type_out_len)
{
	size_t i;
	size_t printable = 0;

	if (len == 0)
		return 0;
	for (i = 0; i < len && i < 256; ++i) {
		unsigned char c = data[i];
		if (c == 0)
			break;
		if (c == '\r' || c == '\n' || c == '\t' || (c >= 32 && c < 127))
			++printable;
	}
	if (printable >= 8 && (data[0] == '[' || (data[0] >= 'A' && data[0] <= 'Z'))) {
		snprintf(type_out, type_out_len, "ini");
		return 1;
	}
	return 0;
}

static int looks_like_map(uint32_t file_size, char *type_out, size_t type_out_len)
{
	if (file_size == 8192) {
		snprintf(type_out, type_out_len, "map");
		return 1;
	}
	return 0;
}

static int looks_like_st16(
    const unsigned char *data, size_t probe_len, uint32_t file_size, char *type_out, size_t type_out_len)
{
	(void)probe_len;
	if (remix_st16_is_native(data, file_size)) {
		snprintf(type_out, type_out_len, "st16");
		return 1;
	}
	return 0;
}

static int looks_like_icn(
    const unsigned char *data, size_t probe_len, uint32_t file_size, char *type_out, size_t type_out_len)
{
	(void)probe_len;
	if (remix_st16_should_convert(data, file_size)) {
		snprintf(type_out, type_out_len, "icn");
		return 1;
	}
	return 0;
}

static int looks_like_shp(const unsigned char *data, size_t len, char *type_out, size_t type_out_len)
{
	uint16_t count;
	uint32_t first_off;

	if (len < 6)
		return 0;
	count = (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
	if (count == 0 || count > 4096)
		return 0;
	if ((size_t)2 + (size_t)count * 4u + 4u > len)
		return 0;
	first_off = read_le32(data + 2);
	if (first_off < (uint32_t)(2 + count * 4) || first_off >= len)
		return 0;
	snprintf(type_out, type_out_len, "shp");
	return 1;
}

void remix_detect_file_type(
    const unsigned char *data, size_t probe_len, uint32_t file_size, char *type_out, size_t type_out_len)
{
	type_out[0] = '\0';
	if (remix_looks_like_aud(data, probe_len, file_size, type_out, type_out_len))
		return;
	if (looks_like_voc(data, probe_len, type_out, type_out_len))
		return;
	if (looks_like_vqa(data, probe_len, type_out, type_out_len))
		return;
	if (looks_like_st16(data, probe_len, file_size, type_out, type_out_len))
		return;
	if (looks_like_icn(data, probe_len, file_size, type_out, type_out_len))
		return;
	if (remix_is_keyframe_shp(data, file_size)) {
		snprintf(type_out, type_out_len, "kshp");
		return;
	}
	if (looks_like_shp(data, probe_len, type_out, type_out_len))
		return;
	if (looks_like_pal(file_size, type_out, type_out_len))
		return;
	if (looks_like_ini(data, probe_len, type_out, type_out_len))
		return;
	if (looks_like_map(file_size, type_out, type_out_len))
		return;
	if (looks_like_pcx(data, probe_len, type_out, type_out_len))
		return;
	snprintf(type_out, type_out_len, "binary");
}

int remix_is_audio_payload(const unsigned char *probe, size_t probe_len, uint32_t file_size)
{
	char type[20];

	if (!remix_looks_like_aud(probe, probe_len, file_size, type, sizeof(type)))
		return 0;
	return 1;
}
