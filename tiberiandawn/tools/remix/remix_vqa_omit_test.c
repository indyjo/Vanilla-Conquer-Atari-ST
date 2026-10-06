/*
 * remix_vqa_omit_test.c — TRANSIT.MIX must drop NOD1PRE.VQA.
 */
#include "remix_vqa.h"

#include <stdio.h>

int main(void)
{
	if (!remix_vqa_should_omit_entry("TRANSIT.MIX", 0x21DD332Fu)
	    || remix_vqa_should_omit_entry("transit.mix", 0x21DD332Fu) == 0) {
		fprintf(stderr, "FAIL nod1pre should be omitted from TRANSIT.MIX\n");
		return 1;
	}
	if (remix_vqa_should_omit_entry("MOVIES.MIX", 0x21DD332Fu)
	    || remix_vqa_should_omit_entry("TRANSIT.MIX", 0x21EC332Fu)
	    || remix_vqa_should_omit_entry(NULL, 0x21DD332Fu)) {
		fprintf(stderr, "FAIL omit denylist too wide\n");
		return 1;
	}
	printf("remix_vqa_omit_test OK\n");
	return 0;
}
