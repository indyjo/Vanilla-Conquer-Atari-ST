#ifndef HISTTOOL_WSA_H
#define HISTTOOL_WSA_H

#include "hist.h"

/* Accumulate 8-bit indexed pixels from all WSA frames into counts.
 * Returns total pixels processed (frames * width * height), or -1 on error. */
long long wsa_hist_accumulate(const char *path, HistCounts *counts);

#endif
