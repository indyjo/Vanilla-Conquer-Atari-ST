# remix - Host utility and MiNT remix.tos for Atari ST MIX repack.

TDROOT = ../..
COMMON = $(TDROOT)/../common
ATARILIB = $(TDROOT)/atarilib

HOST_CC ?= cc
HOST_CXX ?= c++
HOST_CFLAGS ?= -O2 -Wall -Wextra -std=c99 -I. -I$(TDROOT) -I$(COMMON) -I$(ATARILIB)
HOST_CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17 -I. -I$(ATARILIB) -I$(TDROOT) -I$(COMMON) -DREMIX_BUILD

CROSS_PREFIX ?= $(HOME)/opt/cross-mint/bin
CROSS_TARGET ?= m68k-atari-mint
MINT_CC = $(CROSS_PREFIX)/$(CROSS_TARGET)-gcc
MINT_CXX = $(CROSS_PREFIX)/$(CROSS_TARGET)-g++
MINT_CFLAGS ?= -O2 -Wall -Wextra -std=c99 -I. -I$(TDROOT) -I$(COMMON) -I$(ATARILIB)
MINT_CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17 -I. -I$(ATARILIB) -I$(TDROOT) -I$(COMMON) -DREMIX_BUILD

COMMON_SRCS = remix_mix.c remix_merge.c remix_detect.c remix_print.c remix_audio.c remix_aud.c remix_unzap.c
COMMON_OBJS = $(COMMON_SRCS:.c=.o)
MINT_COMMON_OBJS = $(COMMON_SRCS:.c=_mint.o)

ST16_CXX_SRCS = remix_st16_host.cpp remix_st16_stubs.cpp \
	$(ATARILIB)/st16_convert.cpp $(ATARILIB)/st16_iconset.cpp $(ATARILIB)/c2p.cpp
SHPX_CXX_SRCS = remix_shpx_host.cpp remix_shpx_stubs.cpp \
	keyframe_remix.cpp remix_lcw.cpp remix_xordelta.cpp
ST16_OBJS = remix_st16_host.o remix_st16_stubs.o st16_convert.o st16_iconset.o c2p.o
SHPX_OBJS = remix_shpx_host.o remix_shpx_stubs.o keyframe_remix.o remix_lcw.o remix_xordelta.o
MINT_ST16_OBJS = remix_st16_host_mint.o remix_st16_stubs_mint.o st16_convert_mint.o st16_iconset_mint.o c2p_mint.o
MINT_SHPX_OBJS = remix_shpx_host_mint.o remix_shpx_stubs_mint.o keyframe_remix_mint.o remix_lcw_mint.o remix_xordelta_mint.o

.PHONY: all clean host mint test-st16 test-shpx

all: host

test-st16: remix_st16_test
	./remix_st16_test

test-shpx: remix_shpx_test
	./remix_shpx_test

host: remix

mint: remix.tos

remix: remix_host.o $(COMMON_OBJS) $(ST16_OBJS) $(SHPX_OBJS)
	$(HOST_CXX) $(HOST_CXXFLAGS) -o $@ remix_host.o $(COMMON_OBJS) $(ST16_OBJS) $(SHPX_OBJS)

remix_st16_test: remix_st16_test.o $(ST16_OBJS)
	$(HOST_CXX) $(HOST_CXXFLAGS) -o $@ remix_st16_test.o $(ST16_OBJS)

remix_shpx_test: remix_shpx_test.o $(SHPX_OBJS)
	$(HOST_CXX) $(HOST_CXXFLAGS) -o $@ remix_shpx_test.o $(SHPX_OBJS)

remix_st16_test.o: remix_st16_test.c remix_st16.h
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ remix_st16_test.c

remix_shpx_test.o: remix_shpx_test.c remix_shpx.h $(COMMON)/keyframe.h
	$(HOST_CXX) $(HOST_CXXFLAGS) -x c++ -c -o $@ remix_shpx_test.c

remix.tos: remix_st_mint.o $(MINT_COMMON_OBJS) $(MINT_ST16_OBJS) $(MINT_SHPX_OBJS)
	$(MINT_CXX) $(MINT_CXXFLAGS) -o $@ remix_st_mint.o $(MINT_COMMON_OBJS) $(MINT_ST16_OBJS) $(MINT_SHPX_OBJS)

remix_host.o: remix_host.c remix.h remix_print.h remix_st16.h
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ remix_host.c

remix_st_mint.o: remix_st.c remix.h remix_print.h remix_st16.h
	$(MINT_CC) $(MINT_CFLAGS) -c -o $@ remix_st.c

%_mint.o: %.c remix.h
	$(MINT_CC) $(MINT_CFLAGS) -c -o $@ $<

%.o: %.c remix.h
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ $<

remix_st16_host.o: remix_st16_host.cpp remix_st16.h
	$(HOST_CXX) $(HOST_CXXFLAGS) -c -o $@ remix_st16_host.cpp

remix_st16_stubs.o: remix_st16_stubs.cpp
	$(HOST_CXX) $(HOST_CXXFLAGS) -c -o $@ remix_st16_stubs.cpp

remix_shpx_stubs.o: remix_shpx_stubs.cpp function.h
	$(HOST_CXX) $(HOST_CXXFLAGS) -DATARI_ST -c -o $@ remix_shpx_stubs.cpp

remix_shpx_host.o: remix_shpx_host.cpp remix_shpx.h function.h
	$(HOST_CXX) $(HOST_CXXFLAGS) -c -o $@ remix_shpx_host.cpp

keyframe_remix.o: keyframe_remix.cpp function.h
	$(HOST_CXX) $(HOST_CXXFLAGS) -DATARI_ST -Wno-error -c -o $@ keyframe_remix.cpp

remix_lcw.o: remix_lcw.cpp remix_decode.h
	$(HOST_CXX) $(HOST_CXXFLAGS) -c -o $@ remix_lcw.cpp

remix_xordelta.o: remix_xordelta.cpp remix_decode.h
	$(HOST_CXX) $(HOST_CXXFLAGS) -c -o $@ remix_xordelta.cpp

st16_convert.o: $(ATARILIB)/st16_convert.cpp
	$(HOST_CXX) $(HOST_CXXFLAGS) -c -o $@ $<

st16_iconset.o: $(ATARILIB)/st16_iconset.cpp
	$(HOST_CXX) $(HOST_CXXFLAGS) -c -o $@ $<

c2p.o: $(ATARILIB)/c2p.cpp
	$(HOST_CXX) $(HOST_CXXFLAGS) -c -o $@ $<

remix_st16_host_mint.o: remix_st16_host.cpp remix_st16.h
	$(MINT_CXX) $(MINT_CXXFLAGS) -c -o $@ remix_st16_host.cpp

remix_st16_stubs_mint.o: remix_st16_stubs.cpp
	$(MINT_CXX) $(MINT_CXXFLAGS) -c -o $@ remix_st16_stubs.cpp

st16_convert_mint.o: $(ATARILIB)/st16_convert.cpp
	$(MINT_CXX) $(MINT_CXXFLAGS) -c -o $@ $<

st16_iconset_mint.o: $(ATARILIB)/st16_iconset.cpp
	$(MINT_CXX) $(MINT_CXXFLAGS) -c -o $@ $<

c2p_mint.o: $(ATARILIB)/c2p.cpp
	$(MINT_CXX) $(MINT_CXXFLAGS) -c -o $@ $<

remix_shpx_host_mint.o: remix_shpx_host.cpp remix_shpx.h function.h
	$(MINT_CXX) $(MINT_CXXFLAGS) -c -o $@ remix_shpx_host.cpp

remix_shpx_stubs_mint.o: remix_shpx_stubs.cpp function.h
	$(MINT_CXX) $(MINT_CXXFLAGS) -DATARI_ST -c -o $@ remix_shpx_stubs.cpp

keyframe_remix_mint.o: keyframe_remix.cpp function.h
	$(MINT_CXX) $(MINT_CXXFLAGS) -DATARI_ST -Wno-error -c -o $@ keyframe_remix.cpp

remix_lcw_mint.o: remix_lcw.cpp remix_decode.h
	$(MINT_CXX) $(MINT_CXXFLAGS) -c -o $@ remix_lcw.cpp

remix_xordelta_mint.o: remix_xordelta.cpp remix_decode.h
	$(MINT_CXX) $(MINT_CXXFLAGS) -c -o $@ remix_xordelta.cpp

clean:
	rm -f remix remix.tos remix_st16_test remix_shpx_test remix_host.o remix_st_mint.o remix_st16_test.o remix_shpx_test.o \
		$(COMMON_OBJS) $(MINT_COMMON_OBJS) $(ST16_OBJS) $(MINT_ST16_OBJS) $(SHPX_OBJS) $(MINT_SHPX_OBJS)
