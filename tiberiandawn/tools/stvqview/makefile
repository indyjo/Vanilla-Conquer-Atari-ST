# stvqview - Freestanding FORM 'STVQ' player for Atari ST/STE (.TOS)

CROSS_PREFIX ?= $(HOME)/opt/cross-mint/bin
CROSS_TARGET ?= m68k-atari-mint
CC = $(CROSS_PREFIX)/$(CROSS_TARGET)-gcc

CFLAGS ?= -O2 -Wall -Wextra -std=c99
CFLAGS += -I.

SRCS = stvqview.c stvq_format.c stvq_hw.c stvq_player.c stvq_prof.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean

all: stvqview.ttp

stvqview.ttp: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c stvq_format.h stvq_hw.h stvq_player.h stvq_prof.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) stvqview.ttp stvqview.tos
