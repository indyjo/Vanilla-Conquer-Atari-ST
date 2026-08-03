/*
 * ST software blitter: interprets ST_Blitter register state (BLIT_iT semantics).
 *
 * The rect loop is specialised at compile time on the halftone op and the X
 * direction, and the per-line word loop is split into first / middle / last so
 * that the endmask selection, the last-word test and the op dispatch all leave
 * the inner loop. An unmasked, unskewed forward blit gets a separate tight path.
 * Semantics are unchanged from the straightforward interpreter this replaces.
 */

#include "st_blit.h"

static ST_Soft_Backend g_soft_backend;

ST_Soft_Backend::ST_Soft_Backend() : ST_Blit_Backend(state_)
{
}

ST_Soft_Backend &ST_Blit_Soft_Backend()
{
	return g_soft_backend;
}

namespace {

/*
 * BLIT_iT ops, with the destination bits outside the endmask preserved.
 * mask/notmask are passed in because the caller hoists them per segment.
 */
template <uint8_t OP>
inline uint16_t ST_Soft_Op(uint16_t d, uint16_t s, uint16_t mask, uint16_t notmask)
{
	const uint16_t sm = (uint16_t)(s & mask);
	const uint16_t keep = (uint16_t)(d & notmask);

	if (OP == 1) {
		/* (d & mask) & (s & mask) == d & sm */
		return (uint16_t)((d & sm) | keep);
	}
	if (OP == 3) {
		return (uint16_t)(sm | keep);
	}
	/* OP == 7: (d & mask) | (s & mask) */
	return (uint16_t)(((uint16_t)((d | s) & mask)) | keep);
}

/* Same three ops with mask == 0xFFFF folded away. */
template <uint8_t OP>
inline uint16_t ST_Soft_Op_Full(uint16_t d, uint16_t s)
{
	if (OP == 1) {
		return (uint16_t)(d & s);
	}
	if (OP == 3) {
		return s;
	}
	return (uint16_t)(d | s);
}

/*
 * One 16-pixel word. MASK/NOTMASK/LAST are compile-time constant at every use,
 * so the endmask pick and the nfsr test fold away per segment.
 */
#define ST_SOFT_WORD(MASK, NOTMASK, LAST)                                          \
	do {                                                                       \
		if (!REVERSE) {                                                    \
			hold = (hold << 16) | (hold >> 16);                        \
		}                                                                  \
		if (!nfsr || !(LAST)) {                                            \
			hold = (hold & 0xFFFF0000u) | *(const uint16_t *)s;         \
			s += src_x_inc;                                            \
		}                                                                  \
		if (REVERSE) {                                                     \
			hold = (hold << 16) | (hold >> 16);                        \
		}                                                                  \
		*(uint16_t *)d = ST_Soft_Op<OP>(*(const uint16_t *)d,               \
		    (uint16_t)(hold >> shift), (MASK), (NOTMASK));                  \
		d += dst_x_inc;                                                    \
	} while (0)

/* Unmasked, unskewed, forward: no hold register, no endmask, no dest read for OP 3. */
template <uint8_t OP>
void ST_Soft_Blit_Fast(
	const uint8_t *s,
	uint8_t *d,
	int16_t src_x_inc,
	int16_t dst_x_inc,
	int16_t src_y_inc,
	int16_t dst_y_inc,
	uint16_t x_count,
	uint16_t y_count)
{
	for (uint16_t line = 0; line < y_count; ++line) {
		for (uint16_t wi = 0; wi < x_count; ++wi) {
			*(uint16_t *)d = ST_Soft_Op_Full<OP>(*(const uint16_t *)d, *(const uint16_t *)s);
			s += src_x_inc;
			d += dst_x_inc;
		}
		s += src_y_inc - src_x_inc;
		d += dst_y_inc - dst_x_inc;
	}
}

template <uint8_t OP, bool REVERSE>
void ST_Soft_Blit_Rect(
	const uint8_t *s,
	uint8_t *d,
	int16_t src_x_inc,
	int16_t dst_x_inc,
	int16_t src_y_inc,
	int16_t dst_y_inc,
	uint16_t x_count,
	uint16_t y_count,
	uint16_t endmask1,
	uint16_t endmask2,
	uint16_t endmask3,
	unsigned shift,
	bool fxsr,
	bool nfsr)
{
	const uint16_t notmask1 = (uint16_t)~endmask1;
	const uint16_t notmask2 = (uint16_t)~endmask2;
	const uint16_t notmask3 = (uint16_t)~endmask3;
	/* x_count >= 2 here whenever the split path runs; middle may be 0. */
	const uint16_t middle = (x_count > 2) ? (uint16_t)(x_count - 2) : 0;
	uint32_t hold = 0;

	for (uint16_t line = 0; line < y_count; ++line) {
		if (x_count > 0) {
			if (fxsr) {
				/* Prime the hold register with one extra source read. */
				if (REVERSE) {
					hold = (hold & 0x0000FFFFu)
						| ((uint32_t) * (const uint16_t *)s << 16);
				} else {
					hold = (hold & 0xFFFF0000u) | *(const uint16_t *)s;
				}
				s += src_x_inc;
			}

			if (x_count == 1) {
				/* Single word is both first and last: endmask1, nfsr applies. */
				ST_SOFT_WORD(endmask1, notmask1, true);
			} else {
				ST_SOFT_WORD(endmask1, notmask1, false);
				for (uint16_t i = 0; i < middle; ++i) {
					ST_SOFT_WORD(endmask2, notmask2, false);
				}
				ST_SOFT_WORD(endmask3, notmask3, true);
			}
		}

		s += src_y_inc - src_x_inc;
		d += dst_y_inc - dst_x_inc;
	}
}

#undef ST_SOFT_WORD

template <uint8_t OP>
inline void ST_Soft_Blit_Op(
	const uint8_t *s,
	uint8_t *d,
	int16_t src_x_inc,
	int16_t dst_x_inc,
	int16_t src_y_inc,
	int16_t dst_y_inc,
	uint16_t x_count,
	uint16_t y_count,
	uint16_t endmask1,
	uint16_t endmask2,
	uint16_t endmask3,
	unsigned shift,
	bool fxsr,
	bool nfsr,
	bool reverse_x)
{
	const bool unmasked = (endmask1 == 0xFFFFu) && (endmask2 == 0xFFFFu) && (endmask3 == 0xFFFFu);

	if (unmasked && shift == 0 && !fxsr && !nfsr && !reverse_x) {
		ST_Soft_Blit_Fast<OP>(
			s, d, src_x_inc, dst_x_inc, src_y_inc, dst_y_inc, x_count, y_count);
		return;
	}
	if (reverse_x) {
		ST_Soft_Blit_Rect<OP, true>(s,
			d,
			src_x_inc,
			dst_x_inc,
			src_y_inc,
			dst_y_inc,
			x_count,
			y_count,
			endmask1,
			endmask2,
			endmask3,
			shift,
			fxsr,
			nfsr);
	} else {
		ST_Soft_Blit_Rect<OP, false>(s,
			d,
			src_x_inc,
			dst_x_inc,
			src_y_inc,
			dst_y_inc,
			x_count,
			y_count,
			endmask1,
			endmask2,
			endmask3,
			shift,
			fxsr,
			nfsr);
	}
}

/* ---------------------------------------------------------------------------
 * All four bitplanes in one pass.
 *
 * In the ST planar layout the four planes of a 16-pixel column are 8 contiguous
 * bytes, so one base pointer with fixed +0/+2/+4/+6 offsets replaces the four
 * separate passes the hardware needs. Loop control, pointer arithmetic and the
 * endmask setup are then paid once per column instead of four times.
 * ------------------------------------------------------------------------- */

#define ST_SOFT_P4_SWAP()                                                          \
	do {                                                                       \
		h0 = (h0 << 16) | (h0 >> 16);                                      \
		h1 = (h1 << 16) | (h1 >> 16);                                      \
		h2 = (h2 << 16) | (h2 >> 16);                                      \
		h3 = (h3 << 16) | (h3 >> 16);                                      \
	} while (0)

#define ST_SOFT_P4_STORE(MASK, NOTMASK)                                            \
	do {                                                                       \
		*(uint16_t *)(d + 0) = ST_Soft_Op<OP>(*(const uint16_t *)(d + 0),   \
		    (uint16_t)(h0 >> shift), (MASK), (NOTMASK));                    \
		*(uint16_t *)(d + 2) = ST_Soft_Op<OP>(*(const uint16_t *)(d + 2),   \
		    (uint16_t)(h1 >> shift), (MASK), (NOTMASK));                    \
		*(uint16_t *)(d + 4) = ST_Soft_Op<OP>(*(const uint16_t *)(d + 4),   \
		    (uint16_t)(h2 >> shift), (MASK), (NOTMASK));                    \
		*(uint16_t *)(d + 6) = ST_Soft_Op<OP>(*(const uint16_t *)(d + 6),   \
		    (uint16_t)(h3 >> shift), (MASK), (NOTMASK));                    \
		d += dst_x_inc;                                                    \
	} while (0)

/*
 * With a pre-shifted source the skew is 0 and the hold register is dead weight:
 * the output word is just the source word. SHIFT0 is a template constant, so the
 * swap, the merge and the shift fold away and only the load remains.
 */
#define ST_SOFT_P4_DIRECT(MASK, NOTMASK)                                           \
	do {                                                                       \
		*(uint16_t *)(d + 0) = ST_Soft_Op<OP>(*(const uint16_t *)(d + 0),   \
		    *(const uint16_t *)(s + 0), (MASK), (NOTMASK));                 \
		*(uint16_t *)(d + 2) = ST_Soft_Op<OP>(*(const uint16_t *)(d + 2),   \
		    *(const uint16_t *)(s + 2), (MASK), (NOTMASK));                 \
		*(uint16_t *)(d + 4) = ST_Soft_Op<OP>(*(const uint16_t *)(d + 4),   \
		    *(const uint16_t *)(s + 4), (MASK), (NOTMASK));                 \
		*(uint16_t *)(d + 6) = ST_Soft_Op<OP>(*(const uint16_t *)(d + 6),   \
		    *(const uint16_t *)(s + 6), (MASK), (NOTMASK));                 \
		s += src_x_inc;                                                    \
		d += dst_x_inc;                                                    \
	} while (0)

#define ST_SOFT_P4_WORD(MASK, NOTMASK, LAST)                                       \
	do {                                                                       \
		if (SHIFT0) {                                                      \
			ST_SOFT_P4_DIRECT((MASK), (NOTMASK));                      \
			break;                                                     \
		}                                                                  \
		if (!REVERSE) {                                                    \
			ST_SOFT_P4_SWAP();                                         \
		}                                                                  \
		if (!nfsr || !(LAST)) {                                            \
			h0 = (h0 & 0xFFFF0000u) | *(const uint16_t *)(s + 0);       \
			h1 = (h1 & 0xFFFF0000u) | *(const uint16_t *)(s + 2);       \
			h2 = (h2 & 0xFFFF0000u) | *(const uint16_t *)(s + 4);       \
			h3 = (h3 & 0xFFFF0000u) | *(const uint16_t *)(s + 6);       \
			s += src_x_inc;                                            \
		}                                                                  \
		if (REVERSE) {                                                     \
			ST_SOFT_P4_SWAP();                                         \
		}                                                                  \
		ST_SOFT_P4_STORE((MASK), (NOTMASK));                               \
	} while (0)

/* Same three ops at 32 bits, with the endmask duplicated into both halves. */
template <uint8_t OP>
inline uint32_t ST_Soft_Op_Long(uint32_t d, uint32_t s, uint32_t mask, uint32_t notmask)
{
	const uint32_t sm = s & mask;
	const uint32_t keep = d & notmask;

	if (OP == 1) {
		return (d & sm) | keep;
	}
	if (OP == 3) {
		return sm | keep;
	}
	return ((d | s) & mask) | keep;
}

/* Same three ops at 32 bits with the mask folded away; OP 3 loses its dest read. */
template <uint8_t OP>
inline uint32_t ST_Soft_Op_Long_Full(uint32_t d, uint32_t s)
{
	if (OP == 1) {
		return d & s;
	}
	if (OP == 3) {
		return s;
	}
	return d | s;
}

#define ST_SOFT_P4_LWORD_FULL()                                                    \
	do {                                                                       \
		*(uint32_t *)(d + 0) = ST_Soft_Op_Long_Full<OP>(                   \
		    *(const uint32_t *)(d + 0), *(const uint32_t *)(s + 0));        \
		*(uint32_t *)(d + 4) = ST_Soft_Op_Long_Full<OP>(                   \
		    *(const uint32_t *)(d + 4), *(const uint32_t *)(s + 4));        \
		s += 8;                                                            \
		d += 8;                                                            \
	} while (0)

#define ST_SOFT_P4_LWORD(MASK, NOTMASK)                                            \
	do {                                                                       \
		*(uint32_t *)(d + 0) = ST_Soft_Op_Long<OP>(*(const uint32_t *)(d + 0), \
		    *(const uint32_t *)(s + 0), (MASK), (NOTMASK));                 \
		*(uint32_t *)(d + 4) = ST_Soft_Op_Long<OP>(*(const uint32_t *)(d + 4), \
		    *(const uint32_t *)(s + 4), (MASK), (NOTMASK));                 \
		s += 8;                                                            \
		d += 8;                                                            \
	} while (0)

/*
 * Pre-shifted and forward: the four planes of a 16-pixel column are 8 contiguous
 * bytes, so two long accesses replace four word accesses on each side. With a
 * full endmask the notmask is 0 and the destination read drops out entirely.
 * The caller guarantees both pointers and both row strides are 4-byte aligned.
 */
template <uint8_t OP, bool FULL_MID>
void ST_Soft_P4_Planar_Long(
	const uint8_t *s,
	uint8_t *d,
	int16_t src_y_inc,
	int16_t dst_y_inc,
	uint16_t x_count,
	uint16_t y_count,
	uint16_t endmask1,
	uint16_t endmask2,
	uint16_t endmask3)
{
	const uint32_t em1 = ((uint32_t)endmask1 << 16) | endmask1;
	const uint32_t em2 = ((uint32_t)endmask2 << 16) | endmask2;
	const uint32_t em3 = ((uint32_t)endmask3 << 16) | endmask3;
	const uint32_t nm1 = ~em1;
	const uint32_t nm2 = ~em2;
	const uint32_t nm3 = ~em3;
	const uint16_t middle = (x_count > 2) ? (uint16_t)(x_count - 2) : 0;

	for (uint16_t line = 0; line < y_count; ++line) {
		if (x_count > 0) {
			if (x_count == 1) {
				ST_SOFT_P4_LWORD(em1, nm1);
			} else {
				ST_SOFT_P4_LWORD(em1, nm1);
				const uint8_t *const dmid_end = d + (int)middle * 8;
				if (FULL_MID && OP == 3) {
					/*
					 * GCC will not emit the post-increment form here —
					 * it keeps a displacement plus a separate bump, 44
					 * cycles per column against 22 (rg-asm, 68030).
					 */
					unsigned long cols =
					    (unsigned long)(dmid_end - d) >> 3;
					if (cols != 0) {
						__asm__ volatile(
						    "1:\n\t"
						    "move.l (%0)+,(%1)+\n\t"
						    "move.l (%0)+,(%1)+\n\t"
						    "subq.l #1,%2\n\t"
						    "bne 1b\n"
						    : "+a"(s), "+a"(d), "+d"(cols)
						    :
						    : "memory", "cc");
					}
				} else if (FULL_MID) {
					while (d != dmid_end) {
						ST_SOFT_P4_LWORD_FULL();
					}
				} else {
					while (d != dmid_end) {
						ST_SOFT_P4_LWORD(em2, nm2);
					}
				}
				ST_SOFT_P4_LWORD(em3, nm3);
			}
		}

		s += src_y_inc - 8;
		d += dst_y_inc - 8;
	}
}

/*
 * Planar source (one source plane per destination plane).
 *
 * Both strides are compile-time constants here: ST_Blit_Prepare_88 and
 * ST_Blit_Prepare_88_Scroll always pass 8, negated for reverse. That frees the
 * two registers that held them, turns the pointer bumps into addq/post-increment
 * and takes dst_x_inc off the stack. Run_Planes verifies the assumption and
 * falls back to the generic per-plane loop if it ever fails to hold.
 */
template <uint8_t OP, bool REVERSE, bool SHIFT0>
void ST_Soft_P4_Planar(
	const uint8_t *s,
	uint8_t *d,
	int16_t src_y_inc,
	int16_t dst_y_inc,
	uint16_t x_count,
	uint16_t y_count,
	uint16_t endmask1,
	uint16_t endmask2,
	uint16_t endmask3,
	unsigned shift,
	bool fxsr,
	bool nfsr)
{
	const int src_x_inc = REVERSE ? -8 : 8;
	const int dst_x_inc = REVERSE ? -8 : 8;
	const uint16_t notmask1 = (uint16_t)~endmask1;
	const uint16_t notmask2 = (uint16_t)~endmask2;
	const uint16_t notmask3 = (uint16_t)~endmask3;
	const uint16_t middle = (x_count > 2) ? (uint16_t)(x_count - 2) : 0;
	uint32_t h0 = 0, h1 = 0, h2 = 0, h3 = 0;

	/*
	 * Long accesses need both sides 4-byte aligned, and the row strides have to
	 * preserve that. Misalignment is legal on the 68020+ but costs more than the
	 * pairing saves, so an unaligned surface stays on the word path.
	 */
	if (SHIFT0 && !REVERSE) {
		const unsigned long mixed = (unsigned long)(uintptr_t)s
			| (unsigned long)(uintptr_t)d
			| (unsigned long)(unsigned short)src_y_inc
			| (unsigned long)(unsigned short)dst_y_inc;
		if ((mixed & 3u) == 0u) {
			/* endmask2 is a runtime value; only as a constant can OP 3 drop
			   the destination read in the middle run. */
			if (endmask2 == 0xFFFFu) {
				ST_Soft_P4_Planar_Long<OP, true>(s, d, src_y_inc,
				    dst_y_inc, x_count, y_count, endmask1, endmask2, endmask3);
			} else {
				ST_Soft_P4_Planar_Long<OP, false>(s, d, src_y_inc,
				    dst_y_inc, x_count, y_count, endmask1, endmask2, endmask3);
			}
			return;
		}
	}

	for (uint16_t line = 0; line < y_count; ++line) {
		if (x_count > 0) {
			if (fxsr) {
				if (REVERSE) {
					h0 = (h0 & 0x0000FFFFu) | ((uint32_t) * (const uint16_t *)(s + 0) << 16);
					h1 = (h1 & 0x0000FFFFu) | ((uint32_t) * (const uint16_t *)(s + 2) << 16);
					h2 = (h2 & 0x0000FFFFu) | ((uint32_t) * (const uint16_t *)(s + 4) << 16);
					h3 = (h3 & 0x0000FFFFu) | ((uint32_t) * (const uint16_t *)(s + 6) << 16);
				} else {
					h0 = (h0 & 0xFFFF0000u) | *(const uint16_t *)(s + 0);
					h1 = (h1 & 0xFFFF0000u) | *(const uint16_t *)(s + 2);
					h2 = (h2 & 0xFFFF0000u) | *(const uint16_t *)(s + 4);
					h3 = (h3 & 0xFFFF0000u) | *(const uint16_t *)(s + 6);
				}
				s += src_x_inc;
			}

			if (x_count == 1) {
				ST_SOFT_P4_WORD(endmask1, notmask1, true);
			} else {
				ST_SOFT_P4_WORD(endmask1, notmask1, false);
				/*
				 * Bound the middle run by the destination pointer, not by a
				 * counter: that becomes a cmpa/branch pair and keeps a data
				 * register free for the endmask instead of the loop index.
				 */
				const uint8_t *const dmid_end = d + (int)middle * dst_x_inc;
				while (d != dmid_end) {
					ST_SOFT_P4_WORD(endmask2, notmask2, false);
				}
				ST_SOFT_P4_WORD(endmask3, notmask3, true);
			}
		}

		s += src_y_inc - src_x_inc;
		d += dst_y_inc - dst_x_inc;
	}
}

/*
 * 1bpp mask source shared by all four planes: one source read per column
 * instead of four, and a single hold register.
 */
template <uint8_t OP, bool SHIFT0>
void ST_Soft_P4_Broadcast(
	const uint8_t *s,
	uint8_t *d,
	int16_t src_y_inc,
	int16_t dst_y_inc,
	uint16_t x_count,
	uint16_t y_count,
	uint16_t endmask1,
	uint16_t endmask2,
	uint16_t endmask3,
	unsigned shift,
	bool fxsr,
	bool nfsr)
{
	/* ST_Blit_Prepare_28: 1bpp mask source steps 2, destination steps 8. */
	const int src_x_inc = 2;
	const int dst_x_inc = 8;
	const uint16_t notmask1 = (uint16_t)~endmask1;
	const uint16_t notmask2 = (uint16_t)~endmask2;
	const uint16_t notmask3 = (uint16_t)~endmask3;
	const uint16_t middle = (x_count > 2) ? (uint16_t)(x_count - 2) : 0;
	uint32_t hold = 0;

#define ST_SOFT_BC_WORD(MASK, NOTMASK, LAST)                                       \
	do {                                                                       \
		uint16_t sv;                                                       \
		if (SHIFT0) {                                                      \
			sv = *(const uint16_t *)s;                                 \
			s += src_x_inc;                                            \
		} else {                                                           \
			hold = (hold << 16) | (hold >> 16);                        \
			if (!nfsr || !(LAST)) {                                    \
				hold = (hold & 0xFFFF0000u) | *(const uint16_t *)s; \
				s += src_x_inc;                                    \
			}                                                          \
			sv = (uint16_t)(hold >> shift);                            \
		}                                                                  \
		*(uint16_t *)(d + 0) = ST_Soft_Op<OP>(*(const uint16_t *)(d + 0), sv, (MASK), (NOTMASK)); \
		*(uint16_t *)(d + 2) = ST_Soft_Op<OP>(*(const uint16_t *)(d + 2), sv, (MASK), (NOTMASK)); \
		*(uint16_t *)(d + 4) = ST_Soft_Op<OP>(*(const uint16_t *)(d + 4), sv, (MASK), (NOTMASK)); \
		*(uint16_t *)(d + 6) = ST_Soft_Op<OP>(*(const uint16_t *)(d + 6), sv, (MASK), (NOTMASK)); \
		d += dst_x_inc;                                                    \
	} while (0)

	for (uint16_t line = 0; line < y_count; ++line) {
		if (x_count > 0) {
			if (fxsr) {
				hold = (hold & 0xFFFF0000u) | *(const uint16_t *)s;
				s += src_x_inc;
			}
			if (x_count == 1) {
				ST_SOFT_BC_WORD(endmask1, notmask1, true);
			} else {
				ST_SOFT_BC_WORD(endmask1, notmask1, false);
				const uint8_t *const dmid_end = d + (int)middle * dst_x_inc;
				while (d != dmid_end) {
					ST_SOFT_BC_WORD(endmask2, notmask2, false);
				}
				ST_SOFT_BC_WORD(endmask3, notmask3, true);
			}
		}
		s += src_y_inc - src_x_inc;
		d += dst_y_inc - dst_x_inc;
	}
#undef ST_SOFT_BC_WORD
}

#undef ST_SOFT_P4_WORD
#undef ST_SOFT_P4_STORE
#undef ST_SOFT_P4_SWAP

} // namespace

void ST_Soft_Backend::Run_Planes(const ST_Blitter &plan, const ST_Blit_Job &job,
    uint16_t lines, bool hog)
{
	(void)hog;
	/* Read straight from the caller's plan: no copy, no volatile. */
	const ST_Blitter &r = plan;

	const int16_t src_x_inc = r.src_x_inc;
	const int16_t dst_x_inc = r.dst_x_inc;
	const int16_t src_y_inc = r.src_y_inc;
	const int16_t dst_y_inc = r.dst_y_inc;
	const uint16_t x_count = r.x_count;
	const uint8_t op = r.op;
	const uint16_t endmask1 = r.endmask1;
	const uint16_t endmask2 = r.endmask2;
	const uint16_t endmask3 = r.endmask3;
	const unsigned shift = (unsigned)(r.skew & 15u);
	const bool fxsr = (r.skew & 0x80u) != 0;
	const bool nfsr = (r.skew & 0x40u) != 0;
	const bool reverse_x = src_x_inc < 0;

	const uint8_t *const s = job.src_plane0;
	uint8_t *const d = job.dst_plane0;

	/*
	 * The specialised paths bake in the strides the prepare helpers produce.
	 * Anything else — including the degenerate single-column case, where
	 * ST_Blit_Prepare_Impl zeroes both increments — takes the generic
	 * per-plane loop, which handles every register image.
	 */
	const bool strides_ok = job.src_addr_per_plane
		? (src_x_inc == (reverse_x ? -8 : 8) && dst_x_inc == (reverse_x ? -8 : 8))
		: (!reverse_x && src_x_inc == 2 && dst_x_inc == 8);
	if (!strides_ok) {
		ST_Blit_Backend::Run_Planes(plan, job, lines, hog);
		return;
	}

	/*
	 * Pre-shifted sources land here with skew 0. fxsr/nfsr only exist to feed the
	 * hold register, so they must be clear too before the direct path is valid.
	 */
	const bool shift0 = (shift == 0u) && !fxsr && !nfsr;

#define ST_SOFT_P4_CALL(OPV, S0)                                                  \
	do {                                                                      \
		if (!job.src_addr_per_plane) {                                    \
			ST_Soft_P4_Broadcast<OPV, S0>(s, d, src_y_inc, dst_y_inc,  \
			    x_count, lines, endmask1, endmask2, endmask3, shift,   \
			    fxsr, nfsr);                                           \
		} else if (reverse_x) {                                           \
			ST_Soft_P4_Planar<OPV, true, S0>(s, d, src_y_inc,          \
			    dst_y_inc, x_count, lines, endmask1, endmask2,         \
			    endmask3, shift, fxsr, nfsr);                          \
		} else {                                                          \
			ST_Soft_P4_Planar<OPV, false, S0>(s, d, src_y_inc,         \
			    dst_y_inc, x_count, lines, endmask1, endmask2,         \
			    endmask3, shift, fxsr, nfsr);                          \
		}                                                                 \
	} while (0)

#define ST_SOFT_P4_DISPATCH(OPV)                                                  \
	do {                                                                      \
		if (shift0) {                                                     \
			ST_SOFT_P4_CALL(OPV, true);                               \
		} else {                                                          \
			ST_SOFT_P4_CALL(OPV, false);                              \
		}                                                                 \
	} while (0)

	switch (op) {
	case 1:
		ST_SOFT_P4_DISPATCH(1);
		break;
	case 3:
		ST_SOFT_P4_DISPATCH(3);
		break;
	case 7:
		ST_SOFT_P4_DISPATCH(7);
		break;
	default:
		/* Leaves the destination unchanged. */
		break;
	}
#undef ST_SOFT_P4_DISPATCH
#undef ST_SOFT_P4_CALL
}
void ST_Soft_Blit_Mask_Merge(
	const uint8_t *m,
	int16_t mask_y_inc,
	const uint8_t *p,
	int16_t planar_y_inc,
	uint8_t *d,
	int16_t dst_y_inc,
	uint16_t x_count,
	uint16_t y_count,
	uint16_t endmask1,
	uint16_t endmask2,
	uint16_t endmask3,
	unsigned shift,
	bool fxsr,
	bool nfsr)
{
	const int mask_x_inc = 2;
	const int planar_x_inc = 8;
	const int dst_x_inc = 8;

	const uint16_t notem1 = (uint16_t)~endmask1;
	const uint16_t notem2 = (uint16_t)~endmask2;
	const uint16_t notem3 = (uint16_t)~endmask3;
	const uint16_t middle = (x_count > 2) ? (uint16_t)(x_count - 2) : 0;

	uint32_t hm = 0;
	uint32_t h0 = 0, h1 = 0, h2 = 0, h3 = 0;

#define ST_SOFT_MRG_WORD(EM, NOTEM, LAST)                                          \
	do {                                                                       \
		hm = (hm << 16) | (hm >> 16);                                      \
		h0 = (h0 << 16) | (h0 >> 16);                                      \
		h1 = (h1 << 16) | (h1 >> 16);                                      \
		h2 = (h2 << 16) | (h2 >> 16);                                      \
		h3 = (h3 << 16) | (h3 >> 16);                                      \
		if (!nfsr || !(LAST)) {                                            \
			hm = (hm & 0xFFFF0000u) | *(const uint16_t *)m;              \
			m += mask_x_inc;                                            \
			h0 = (h0 & 0xFFFF0000u) | *(const uint16_t *)(p + 0);        \
			h1 = (h1 & 0xFFFF0000u) | *(const uint16_t *)(p + 2);        \
			h2 = (h2 & 0xFFFF0000u) | *(const uint16_t *)(p + 4);        \
			h3 = (h3 & 0xFFFF0000u) | *(const uint16_t *)(p + 6);        \
			p += planar_x_inc;                                          \
		}                                                                  \
		const uint16_t a_keep = (uint16_t)((uint16_t)(hm >> shift) | (NOTEM)); \
		uint16_t *const dw = (uint16_t *)d;                                \
		dw[0] = (uint16_t)((dw[0] & a_keep) | ((uint16_t)(h0 >> shift) & (EM))); \
		dw[1] = (uint16_t)((dw[1] & a_keep) | ((uint16_t)(h1 >> shift) & (EM))); \
		dw[2] = (uint16_t)((dw[2] & a_keep) | ((uint16_t)(h2 >> shift) & (EM))); \
		dw[3] = (uint16_t)((dw[3] & a_keep) | ((uint16_t)(h3 >> shift) & (EM))); \
		d += dst_x_inc;                                                    \
	} while (0)

	for (uint16_t line = 0; line < y_count; ++line) {
		if (x_count > 0) {
			if (fxsr) {
				hm = (hm & 0xFFFF0000u) | *(const uint16_t *)m;
				m += mask_x_inc;
				h0 = (h0 & 0xFFFF0000u) | *(const uint16_t *)(p + 0);
				h1 = (h1 & 0xFFFF0000u) | *(const uint16_t *)(p + 2);
				h2 = (h2 & 0xFFFF0000u) | *(const uint16_t *)(p + 4);
				h3 = (h3 & 0xFFFF0000u) | *(const uint16_t *)(p + 6);
				p += planar_x_inc;
			}

			if (x_count == 1) {
				ST_SOFT_MRG_WORD(endmask1, notem1, true);
			} else {
				ST_SOFT_MRG_WORD(endmask1, notem1, false);
				const uint8_t *const dmid_end = d + (int)middle * dst_x_inc;
				while (d != dmid_end) {
					ST_SOFT_MRG_WORD(endmask2, notem2, false);
				}
				ST_SOFT_MRG_WORD(endmask3, notem3, true);
			}
		}

		m += mask_y_inc - mask_x_inc;
		p += planar_y_inc - planar_x_inc;
		d += dst_y_inc - dst_x_inc;
	}
#undef ST_SOFT_MRG_WORD
}

void ST_Soft_Backend::Await()
{
}

void ST_Soft_Backend::Execute(bool hog, uint16_t lines, void *src_addr, void *dst_addr)
{
	(void)hog;
	ST_Blitter &r = plan_;
	r.src_addr = src_addr;
	r.dst_addr = dst_addr;
	r.y_count = lines;

	const int16_t src_x_inc = r.src_x_inc;
	const int16_t dst_x_inc = r.dst_x_inc;
	const int16_t src_y_inc = r.src_y_inc;
	const int16_t dst_y_inc = r.dst_y_inc;
	const uint16_t x_count = r.x_count;
	const uint16_t y_count = lines;
	const uint8_t op = r.op;
	const uint16_t endmask1 = r.endmask1;
	const uint16_t endmask2 = r.endmask2;
	const uint16_t endmask3 = r.endmask3;
	const unsigned shift = (unsigned)(r.skew & 15u);
	const bool fxsr = (r.skew & 0x80u) != 0;
	const bool nfsr = (r.skew & 0x40u) != 0;
	const bool reverse_x = src_x_inc < 0;

	const uint8_t *s = (const uint8_t *)src_addr;
	uint8_t *d = (uint8_t *)dst_addr;

	switch (op) {
	case 1:
		ST_Soft_Blit_Op<1>(s, d, src_x_inc, dst_x_inc, src_y_inc, dst_y_inc, x_count,
			y_count, endmask1, endmask2, endmask3, shift, fxsr, nfsr, reverse_x);
		break;
	case 3:
		ST_Soft_Blit_Op<3>(s, d, src_x_inc, dst_x_inc, src_y_inc, dst_y_inc, x_count,
			y_count, endmask1, endmask2, endmask3, shift, fxsr, nfsr, reverse_x);
		break;
	case 7:
		ST_Soft_Blit_Op<7>(s, d, src_x_inc, dst_x_inc, src_y_inc, dst_y_inc, x_count,
			y_count, endmask1, endmask2, endmask3, shift, fxsr, nfsr, reverse_x);
		break;
	default:
		/* Every other op leaves the destination unchanged; nothing to write. */
		break;
	}
}
