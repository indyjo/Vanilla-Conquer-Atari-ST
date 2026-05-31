/*
 * timer.h - Atari ST/MiNT: redirect to common/timer.h (real countdown timers).
 *
 * Do not #define TIMER_H here before the include — common/timer.h uses that guard
 * and would be skipped, leaving the old stub behaviour (Time() always 0).
 */
#include "../../common/timer.h"
