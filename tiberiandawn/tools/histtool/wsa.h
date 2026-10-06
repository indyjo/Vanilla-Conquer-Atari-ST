#ifndef HISTTOOL_WSA_H
#define HISTTOOL_WSA_H

#include "hist.h"

/* Westwood LCW (Format 80). Returns bytes written, or 0 on failure. */
unsigned long lcw_uncompress(const unsigned char *source, unsigned char *dest, unsigned long length);

/* Accumulate 8-bit indexed pixels from all WSA frames into counts.
 * Returns total pixels processed (frames * width * height), or -1 on error. */
long long wsa_hist_accumulate(const char *path, HistCounts *counts);

#endif
