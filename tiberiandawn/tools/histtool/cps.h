#ifndef HISTTOOL_CPS_H
#define HISTTOOL_CPS_H

#include "hist.h"

/* Accumulate 8-bit indexed pixels from a Westwood CPS still into counts.
 * Returns pixels processed, or -1 on error. */
long long cps_hist_accumulate(const char *path, HistCounts *counts);

#endif
