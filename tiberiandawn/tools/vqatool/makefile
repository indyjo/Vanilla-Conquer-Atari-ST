# Host-side VQA / STVQ utility (not built for m68k-atari-mint).

CC ?= cc
CXX ?= c++
COMMON = ../../../common
CFLAGS ?= -O2 -Wall -Wextra -std=c99
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17
CPPFLAGS = -I$(COMMON) -I. -I../palette-opt
LDFLAGS ?=

C_SRCS = \
	vqatool.c \
	vqa_format.c \
	vqa_io.c \
	vqa_inspect.c \
	vqa_decode.c \
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
	stvq_fix_w16.c

OBJS = $(C_SRCS:.c=.o) palette_color.o lcw.o vqa_lcw_bridge.o unvqbuff.o vqa_unvq_bridge.o

vqatool: $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) -lm

palette_color.o: ../palette-opt/palette_color.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

lcw.o: $(COMMON)/lcw.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

unvqbuff.o: $(COMMON)/unvqbuff.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

vqa_lcw_bridge.o: vqa_lcw_bridge.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

vqa_unvq_bridge.o: vqa_unvq_bridge.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -f vqatool $(OBJS)

.PHONY: clean
