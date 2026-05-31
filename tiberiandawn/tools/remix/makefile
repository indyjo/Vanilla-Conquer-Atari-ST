# Host utility: repack C&C MIX archives for the Atari ST port.

CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c99

remix: remix.o remix_aud.o
	$(CC) $(LDFLAGS) -o $@ remix.o remix_aud.o

remix.o: remix.c remix_aud.h
remix_aud.o: remix_aud.c remix_aud.h

clean:
	rm -f remix remix.o remix_aud.o

.PHONY: clean
