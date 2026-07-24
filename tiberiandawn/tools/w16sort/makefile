# Host utility: reorder .W16 subset pens along a YUV Hamiltonian path.

CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c99

w16sort: w16sort.c
	$(CC) $(CFLAGS) -o $@ w16sort.c -lm

clean:
	rm -f w16sort

.PHONY: clean
