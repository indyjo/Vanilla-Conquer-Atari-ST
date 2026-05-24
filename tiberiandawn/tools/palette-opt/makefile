# Host-side utility (not built for m68k-atari-mint).
# Imported from STDOOM (palette-opt); see README.MD.

CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c99
# macOS clang needs libomp; Linux/gcc usually has -fopenmp built in.
ifeq ($(shell uname -s),Darwin)
  OPENMP ?= 0
else
  OPENMP ?= 1
endif

ifeq ($(OPENMP),1)
  CFLAGS += -fopenmp
  LDFLAGS += -fopenmp
endif

palette-opt: main.o playpal.o palette_color.o subset_fix.o subset_spread.o subset_opt.o weight_opt.o ply_dump.o
	$(CC) $(LDFLAGS) -o $@ $+ -lm

clean:
	rm -f palette-opt *.o

.PHONY: clean
