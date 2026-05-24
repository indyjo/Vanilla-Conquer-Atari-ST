# Host-side utility (not built for m68k-atari-mint).

CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c99

histtool: histtool.o hist.o bmp.o wsa.o chart.o
	$(CC) $(LDFLAGS) -o $@ $^

clean:
	rm -f histtool *.o

.PHONY: clean
