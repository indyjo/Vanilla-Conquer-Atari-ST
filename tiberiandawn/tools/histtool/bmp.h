#ifndef HISTTOOL_BMP_H
#define HISTTOOL_BMP_H

#include "hist.h"

/* Accumulate 8-bit indexed BMP pixels into counts. Returns pixels processed, or -1 on error. */
long long bmp_hist_accumulate(const char *path, HistCounts *counts);

#endif
