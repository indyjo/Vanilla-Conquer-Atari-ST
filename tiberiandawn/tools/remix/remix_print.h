#ifndef REMIX_PRINT_H
#define REMIX_PRINT_H

#include "remix.h"

void remix_print_st_banner(void);
void remix_print_st_mix_header(const char *in_path, unsigned count);
void remix_print_st_mix_done(const char *path, unsigned count, uint32_t data_size);
void remix_print_st_entry_line(uint32_t crc, uint32_t size, const char *type_in, const char *type_out);
void remix_print_st_progress(const char *verb, unsigned done, unsigned total);
void remix_print_st_progress_erase(void);
void remix_print_st_progress_reset(void);
void remix_print_st_warn(const char *msg);
void remix_print_st_await_keypress(void);
void remix_print_st_summary(const RemixStats *stats);

void remix_print_host_banner(const char *in_path, const char *out_path, unsigned count, uint32_t data_start);
void remix_print_host_table_header(void);
void remix_print_host_entry(const RemixEntry *e);
void remix_print_host_mix_done(const char *path, unsigned count, uint32_t data_size);

#endif /* REMIX_PRINT_H */
