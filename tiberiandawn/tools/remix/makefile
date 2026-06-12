# remix - Host utility and MiNT remix.tos for Atari ST MIX repack.

HOST_CC ?= cc
HOST_CFLAGS ?= -O2 -Wall -Wextra -std=c99 -I.

CROSS_PREFIX ?= $(HOME)/opt/cross-mint/bin
CROSS_TARGET ?= m68k-atari-mint
MINT_CC = $(CROSS_PREFIX)/$(CROSS_TARGET)-gcc
MINT_CFLAGS ?= -O2 -Wall -Wextra -std=c99 -I.

COMMON_SRCS = remix_mix.c remix_detect.c remix_print.c remix_audio.c remix_aud.c remix_unzap.c
COMMON_OBJS = $(COMMON_SRCS:.c=.o)
MINT_COMMON_OBJS = $(COMMON_SRCS:.c=_mint.o)

.PHONY: all clean host mint

all: host

host: remix

mint: remix.tos

remix: remix_host.o $(COMMON_OBJS)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ remix_host.o $(COMMON_OBJS)

remix.tos: remix_st_mint.o $(MINT_COMMON_OBJS)
	$(MINT_CC) $(MINT_CFLAGS) -o $@ remix_st_mint.o $(MINT_COMMON_OBJS)

remix_host.o: remix_host.c remix.h remix_print.h
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ remix_host.c

remix_st_mint.o: remix_st.c remix.h remix_print.h
	$(MINT_CC) $(MINT_CFLAGS) -c -o $@ remix_st.c

%_mint.o: %.c remix.h
	$(MINT_CC) $(MINT_CFLAGS) -c -o $@ $<

%.o: %.c remix.h
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ $<

clean:
	rm -f remix remix.tos remix_host.o remix_st.o remix_st_mint.o $(COMMON_OBJS) $(MINT_COMMON_OBJS)
