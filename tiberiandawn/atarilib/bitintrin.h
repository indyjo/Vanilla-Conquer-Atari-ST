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

/*
** Packed bit array: 68000 btst/bset with a word byte-offset.
** Memory btst/bset use Dn modulo 8, so the full bit index can stay in Dn.
** Caller must pass a live buffer and bit >= 0.
*/
static inline void Bset_Bit_U16(unsigned char* array, unsigned short bit)
{
	unsigned short byte_off;
	__asm__ volatile("move.w %2,%0\n\t"
	                 "lsr.w #3,%0\n\t"
	                 "bset.b %2,(%1,%0.w)"
	                 : "=&d"(byte_off)
	                 : "a"(array), "d"(bit)
	                 : "memory", "cc");
}

static inline void Bclr_Bit_U16(unsigned char* array, unsigned short bit)
{
	unsigned short byte_off;
	__asm__ volatile("move.w %2,%0\n\t"
	                 "lsr.w #3,%0\n\t"
	                 "bclr.b %2,(%1,%0.w)"
	                 : "=&d"(byte_off)
	                 : "a"(array), "d"(bit)
	                 : "memory", "cc");
}

static inline int Btst_Bit_U16(unsigned char const* array, unsigned short bit)
{
	unsigned short byte_off;
	unsigned char flagged;
	__asm__ volatile("move.w %3,%1\n\t"
	                 "lsr.w #3,%1\n\t"
	                 "btst.b %3,(%2,%1.w)\n\t"
	                 "sne.b %0"
	                 : "=d"(flagged), "=&d"(byte_off)
	                 : "a"(array), "d"(bit)
	                 : "cc");
	return flagged;
}

#endif /* ATARILIB_BITINTRIN_H */
