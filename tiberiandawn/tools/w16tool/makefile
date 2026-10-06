# Host utility: inspect .W16 files, dither BMP/CPS images, and draw palette matrices.

CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c99

w16tool: w16tool.c
	$(CC) $(CFLAGS) -o $@ w16tool.c

clean:
	rm -f w16tool

.PHONY: clean
