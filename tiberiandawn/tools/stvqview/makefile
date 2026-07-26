# stvqview - Freestanding FORM 'STVQ' player for Atari ST/STE (.TOS)
# Links shared runtime from ../../atarilib/stvq/

CROSS_PREFIX ?= $(HOME)/opt/cross-mint/bin
CROSS_TARGET ?= m68k-atari-mint
CC = $(CROSS_PREFIX)/$(CROSS_TARGET)-gcc

STVQ_DIR = ../../atarilib/stvq

CFLAGS ?= -O2 -Wall -Wextra -std=c99
CFLAGS += -I. -I$(STVQ_DIR)

CLI_SRCS = stvqview.c
LIB_SRCS = $(STVQ_DIR)/stvq_format.c $(STVQ_DIR)/stvq_hw.c $(STVQ_DIR)/stvq_player.c $(STVQ_DIR)/stvq_prof.c
CLI_OBJS = $(CLI_SRCS:.c=.o)
LIB_OBJS = stvq_format.o stvq_hw.o stvq_player.o stvq_prof.o
OBJS = $(CLI_OBJS) $(LIB_OBJS)

.PHONY: all clean

all: stvqview.ttp

stvqview.ttp: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

stvq_format.o: $(STVQ_DIR)/stvq_format.c
	$(CC) $(CFLAGS) -c -o $@ $<

stvq_hw.o: $(STVQ_DIR)/stvq_hw.c
	$(CC) $(CFLAGS) -c -o $@ $<

stvq_player.o: $(STVQ_DIR)/stvq_player.c
	$(CC) $(CFLAGS) -c -o $@ $<

stvq_prof.o: $(STVQ_DIR)/stvq_prof.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) stvqview.ttp stvqview.tos
