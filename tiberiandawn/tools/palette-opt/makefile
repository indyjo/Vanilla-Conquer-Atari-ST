# Host-side utility (not built for m68k-atari-mint).
# Imported from STDOOM (palette-opt); see README.MD.

CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c99

palette-opt: main.o playpal.o palette_color.o subset_spread.o ply_dump.o
	$(CC) $(LDFLAGS) -o $@ $+ -lm

clean:
	rm -f palette-opt *.o

.PHONY: clean
