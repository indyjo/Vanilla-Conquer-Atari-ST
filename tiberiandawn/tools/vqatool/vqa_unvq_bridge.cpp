/*
 * vqa_unvq_bridge.cpp - C-callable UnVQ_4x2.
 */
#include "unvqbuff.h"

extern "C" void vqa_unvq_4x2(uint8_t *codebook, uint8_t *pointers, uint8_t *buffer,
    unsigned blocks_per_row, unsigned num_rows, unsigned buff_width)
{
	UnVQ_4x2(codebook, pointers, buffer, blocks_per_row, num_rows, buff_width);
}
