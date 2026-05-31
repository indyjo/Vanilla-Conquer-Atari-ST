/*
 * bitintrin.h - Inline bit helpers for BooleanVectorClass (680x0 ST port)
 */

#ifndef ATARILIB_BITINTRIN_H
#define ATARILIB_BITINTRIN_H

#ifdef __cplusplus
extern "C" {
#endif
int First_True_Bit(void const * array);
int First_False_Bit(void const * array);
#ifdef __cplusplus
}
#endif

static inline void Set_Bit(void * array, int bit, int value)
{
	if (!array) return;
	if (bit < 0) return;

	unsigned char *byte_array = (unsigned char *)array;
	int byte_index = bit >> 3;
	int bit_index  = bit & 0x7;

	unsigned char mask = (unsigned char)~(1U << bit_index);
	byte_array[byte_index] &= mask;

	if (value) {
		mask = (unsigned char)(1U << bit_index);
		byte_array[byte_index] |= mask;
	}
}

static inline int Get_Bit(void const * array, int bit)
{
	if (!array) return 0;
	if (bit < 0) return 0;

	unsigned char const *byte_array = (unsigned char const *)array;
	int byte_index = bit >> 3;
	int bit_index  = bit & 0x7;

	return (byte_array[byte_index] >> bit_index) & 1;
}

#endif /* ATARILIB_BITINTRIN_H */
