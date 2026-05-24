# Host utility: reorder .W16 subset pens and remap weight columns.

CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c99

w16fix: w16fix.c
	$(CC) $(CFLAGS) -o $@ w16fix.c

clean:
	rm -f w16fix

.PHONY: clean
