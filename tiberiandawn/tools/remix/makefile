# remix - Host utility for Atari ST MIX repack (audio, ST16, SHPX, STVQ).

TDROOT = ../..
COMMON = $(TDROOT)/../common
ATARILIB = $(TDROOT)/atarilib
VQATOOL = ../vqatool
PALETTE_OPT = ../palette-opt

HOST_CC ?= cc
HOST_CXX ?= c++
HOST_CFLAGS ?= -O2 -Wall -Wextra -std=c99 -I. -I$(TDROOT) -I$(COMMON) -I$(ATARILIB) -I$(VQATOOL) -I$(PALETTE_OPT)
HOST_CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17 -I. -I$(ATARILIB) -I$(TDROOT) -I$(COMMON) -I$(VQATOOL) -DREMIX_BUILD

COMMON_SRCS = remix_mix.c remix_merge.c remix_detect.c remix_print.c remix_audio.c remix_aud.c remix_audx.c remix_unzap.c remix_vqa.c
COMMON_OBJS = $(COMMON_SRCS:.c=.o)

ST16_CXX_SRCS = remix_st16_host.cpp remix_st16_stubs.cpp \
	$(ATARILIB)/st16_convert.cpp $(ATARILIB)/st16_iconset.cpp $(ATARILIB)/c2p.cpp
SHPX_CXX_SRCS = remix_shpx_host.cpp remix_shpx_stubs.cpp \
	keyframe_remix.cpp remix_lcw.cpp remix_xordelta.cpp
ST16_OBJS = remix_st16_host.o remix_st16_stubs.o st16_convert.o st16_iconset.o c2p.o
SHPX_OBJS = remix_shpx_host.o remix_shpx_stubs.o keyframe_remix.o remix_lcw.o remix_xordelta.o

STVQ_C_SRCS = \
	$(VQATOOL)/vqa_format.c \
	$(VQATOOL)/vqa_io.c \
	$(VQATOOL)/vqa_decode.c \
	$(VQATOOL)/stvq_format.c \
	$(VQATOOL)/stvq_write.c \
	$(VQATOOL)/stvq_palette.c \
	$(VQATOOL)/stvq_c2p.c \
	$(VQATOOL)/stvq_metric.c \
	$(VQATOOL)/stvq_codebook.c \
	$(VQATOOL)/stvq_encode.c
STVQ_C_OBJS = vqa_format.o vqa_io.o vqa_decode.o stvq_format.o stvq_write.o stvq_palette.o \
	stvq_c2p.o stvq_metric.o stvq_codebook.o stvq_encode.o
STVQ_CXX_OBJS = palette_color.o lcw_common.o vqa_lcw_bridge.o unvqbuff.o vqa_unvq_bridge.o

.PHONY: all clean host test test-st16 test-shpx test-audx

all: host

test: test-st16 test-shpx test-audx

test-st16: remix_st16_test
	./remix_st16_test

test-shpx: remix_shpx_test
	./remix_shpx_test

test-audx: remix_audx_test
	./remix_audx_test

host: remix

remix: remix_host.o $(COMMON_OBJS) $(ST16_OBJS) $(SHPX_OBJS) $(STVQ_C_OBJS) $(STVQ_CXX_OBJS)
	$(HOST_CXX) $(HOST_CXXFLAGS) -o $@ remix_host.o $(COMMON_OBJS) $(ST16_OBJS) $(SHPX_OBJS) \
		$(STVQ_C_OBJS) $(STVQ_CXX_OBJS) -lm

remix_st16_test: remix_st16_test.o $(ST16_OBJS)
	$(HOST_CXX) $(HOST_CXXFLAGS) -o $@ remix_st16_test.o $(ST16_OBJS)

remix_shpx_test: remix_shpx_test.o $(SHPX_OBJS)
	$(HOST_CXX) $(HOST_CXXFLAGS) -o $@ remix_shpx_test.o $(SHPX_OBJS)

remix_audx_test: remix_audx_test.o remix_audx.o
	$(HOST_CC) $(HOST_CFLAGS) -o $@ remix_audx_test.o remix_audx.o

remix_audx_test.o: remix_audx_test.c remix_audx.h
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ remix_audx_test.c

remix_st16_test.o: remix_st16_test.c remix_st16.h
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ remix_st16_test.c

remix_shpx_test.o: remix_shpx_test.c remix_shpx.h $(COMMON)/keyframe.h
	$(HOST_CXX) $(HOST_CXXFLAGS) -x c++ -c -o $@ remix_shpx_test.c

remix_host.o: remix_host.c remix.h remix_print.h remix_st16.h
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ remix_host.c

%.o: %.c remix.h
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ $<

remix_vqa.o: remix_vqa.c remix_vqa.h remix.h
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ remix_vqa.c

vqa_format.o: $(VQATOOL)/vqa_format.c
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ $<
vqa_io.o: $(VQATOOL)/vqa_io.c
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ $<
vqa_decode.o: $(VQATOOL)/vqa_decode.c
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ $<
stvq_format.o: $(VQATOOL)/stvq_format.c
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ $<
stvq_write.o: $(VQATOOL)/stvq_write.c
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ $<
stvq_palette.o: $(VQATOOL)/stvq_palette.c
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ $<
stvq_c2p.o: $(VQATOOL)/stvq_c2p.c
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ $<
stvq_metric.o: $(VQATOOL)/stvq_metric.c
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ $<
stvq_codebook.o: $(VQATOOL)/stvq_codebook.c
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ $<
stvq_encode.o: $(VQATOOL)/stvq_encode.c
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ $<

palette_color.o: $(PALETTE_OPT)/palette_color.c
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ $<
lcw_common.o: $(COMMON)/lcw.cpp
	$(HOST_CXX) $(HOST_CXXFLAGS) -c -o $@ $<
unvqbuff.o: $(COMMON)/unvqbuff.cpp
	$(HOST_CXX) $(HOST_CXXFLAGS) -c -o $@ $<
vqa_lcw_bridge.o: $(VQATOOL)/vqa_lcw_bridge.cpp
	$(HOST_CXX) $(HOST_CXXFLAGS) -c -o $@ $<
vqa_unvq_bridge.o: $(VQATOOL)/vqa_unvq_bridge.cpp
	$(HOST_CXX) $(HOST_CXXFLAGS) -c -o $@ $<

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

clean:
	rm -f remix remix_st16_test remix_shpx_test remix_audx_test remix_host.o remix_st16_test.o remix_shpx_test.o \
		remix_audx_test.o $(COMMON_OBJS) $(ST16_OBJS) $(SHPX_OBJS) $(STVQ_C_OBJS) $(STVQ_CXX_OBJS)
