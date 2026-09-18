# Host-side VQA / STVQ utility (not built for m68k-atari-mint).

CC ?= cc
CXX ?= c++
COMMON = ../../../common
HOST_AUDIO = ../host_audio
LSRC = ../third_party/libsamplerate
CFLAGS ?= -O2 -Wall -Wextra -std=c99
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17
CPPFLAGS = -I$(COMMON) -I. -I../palette-opt -I$(HOST_AUDIO) -I$(LSRC) -I$(LSRC)/include -I$(LSRC)/src -DHAVE_CONFIG_H
DEPFLAGS = -MMD -MP
LDFLAGS ?=

C_SRCS = \
	vqatool.c \
	vqa_format.c \
	vqa_io.c \
	vqa_inspect.c \
	vqa_decode.c \
	vqa_dump.c \
	stvq_format.c \
	stvq_write.c \
	stvq_palette.c \
	stvq_c2p.c \
	stvq_metric.c \
	stvq_codebook.c \
	stvq_encode.c \
	stvq_play.c \
	stvq_preview.c \
	stvq_init_w16.c \
	stvq_refine_w16.c \
	stvq_fix_w16.c \
	$(HOST_AUDIO)/st_host_resample.c \
	$(LSRC)/src/samplerate.c \
	$(LSRC)/src/src_sinc.c \
	$(LSRC)/src/src_linear.c \
	$(LSRC)/src/src_zoh.c

OBJS = $(C_SRCS:.c=.o) palette_color.o lcw.o vqa_lcw_bridge.o unvqbuff.o vqa_unvq_bridge.o
DEPS = $(OBJS:.o=.d)

vqatool: $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) -lm

palette_color.o: ../palette-opt/palette_color.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

lcw.o: $(COMMON)/lcw.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

unvqbuff.o: $(COMMON)/unvqbuff.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

vqa_lcw_bridge.o: vqa_lcw_bridge.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

vqa_unvq_bridge.o: vqa_unvq_bridge.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

clean:
	rm -f vqatool $(OBJS) $(DEPS)

-include $(DEPS)

.PHONY: clean
