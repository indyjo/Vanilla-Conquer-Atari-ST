#ifndef REMIX_DETECT_H
#define REMIX_DETECT_H

#include <stddef.h>
#include <stdint.h>

int remix_looks_like_aud(
    const unsigned char *data, size_t probe_len, uint32_t file_size, char *type_out, size_t type_out_len);

void remix_detect_file_type(
    const unsigned char *data, size_t probe_len, uint32_t file_size, char *type_out, size_t type_out_len);

int remix_is_audio_payload(
    const unsigned char *probe, size_t probe_len, uint32_t file_size);

void remix_format_aud_type(char *type_out, size_t type_out_len, const unsigned char *hdr, const char *codec);
void remix_format_aud_pcm_target(char *type_out, size_t type_out_len);

unsigned short remix_aud_normalize_rate(unsigned short rate);

const char *remix_aud_fail_hint(const unsigned char *hdr, size_t hdr_len, uint32_t file_size);

#endif /* REMIX_DETECT_H */
