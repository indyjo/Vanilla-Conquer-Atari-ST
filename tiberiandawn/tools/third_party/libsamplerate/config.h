/*
 * Project-local config.h for vendored libsamplerate 0.2.2 (not from upstream).
 * Compile samplerate sources with -DHAVE_CONFIG_H and -I this directory.
 */
#ifndef LIBSAMPLERATE_CONFIG_H
#define LIBSAMPLERATE_CONFIG_H

#define PACKAGE "libsamplerate"
#define VERSION "0.2.2"

#define CPU_CLIPS_NEGATIVE 0
#define CPU_CLIPS_POSITIVE 0
#define CPU_IS_BIG_ENDIAN 0
#define CPU_IS_LITTLE_ENDIAN 1

#define HAVE_STDBOOL_H 1
#define HAVE_STDINT_H 1
#define HAVE_LRINT 1
#define HAVE_LRINTF 1

#define ENABLE_SINC_FAST_CONVERTER 1
#define ENABLE_SINC_MEDIUM_CONVERTER 1
#define ENABLE_SINC_BEST_CONVERTER 1

#define SIZEOF_INT 4

#endif
