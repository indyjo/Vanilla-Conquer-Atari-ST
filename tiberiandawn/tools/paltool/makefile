# Host-side utility (not built for m68k-atari-mint).

CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c99

paltool: paltool.o
	$(CC) $(LDFLAGS) -o $@ $^

clean:
	rm -f paltool *.o

.PHONY: clean
