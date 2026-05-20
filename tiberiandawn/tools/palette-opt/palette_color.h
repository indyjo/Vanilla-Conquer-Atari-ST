/*
 * palette_color.h - Gamma + scaled YUV used by palette-opt error metric.
 */

#ifndef PALETTE_OPT_COLOR_H
#define PALETTE_OPT_COLOR_H

/*
 * pal768: 256 × RGB, channels 0..63 (VGA 6-bit).
 * colors: 768 floats written as (2*y, u, v) per index — same as find_best_dist().
 */
void palette_build_opt_colors(const unsigned char *pal768, float colors[768]);

#endif /* PALETTE_OPT_COLOR_H */
