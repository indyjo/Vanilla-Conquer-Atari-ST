# remix-web WASM — compile remix/ sources with Emscripten.
# Requires: source ~/emsdk/emsdk_env.sh  (or: make -C .. wasm from remix-web/)

REMIX_DIR = ../../remix
TDROOT = ../../..
COMMON = $(TDROOT)/../common
ATARILIB = $(TDROOT)/atarilib
OUT_DIR = ../web/public

EMCC ?= emcc
EMCFLAGS = -O2 -Wall -Wextra -std=c99 -I$(REMIX_DIR) -I$(TDROOT) -I$(COMMON) -I$(ATARILIB)
EMCXXFLAGS = -O2 -Wall -Wextra -std=c++17 -I$(REMIX_DIR) -I$(ATARILIB) -I$(TDROOT) -I$(COMMON) -DREMIX_BUILD

COMMON_SRCS = \
	$(REMIX_DIR)/remix_mix.c \
	$(REMIX_DIR)/remix_merge.c \
	$(REMIX_DIR)/remix_detect.c \
	$(REMIX_DIR)/remix_print.c \
	$(REMIX_DIR)/remix_audio.c \
	$(REMIX_DIR)/remix_aud.c \
	$(REMIX_DIR)/remix_unzap.c

ST16_OBJS = remix_st16_host.o remix_st16_stubs.o st16_convert.o st16_iconset.o c2p.o
SHPX_OBJS = remix_shpx_host.o remix_shpx_stubs.o keyframe_remix.o remix_lcw.o remix_xordelta.o

GLUE = remix_wasm.c

EXPORTS = _remix_wasm_process_mix,_remix_wasm_merge_and_process_mix,_remix_wasm_free,_remix_wasm_cleanup_workfiles,_remix_wasm_last_entry_count,_remix_wasm_last_entries,_remix_wasm_set_st16_enabled,_remix_wasm_set_shpx_enabled,_remix_wasm_set_mix_basename,_remix_wasm_install_w16,_remix_wasm_memfs_input,_malloc,_free

.PHONY: all clean

all: $(OUT_DIR)/remix.js $(OUT_DIR)/remix.wasm

$(OUT_DIR)/remix.js $(OUT_DIR)/remix.wasm: $(COMMON_SRCS) $(ST16_OBJS) $(SHPX_OBJS) $(GLUE)
	@mkdir -p $(OUT_DIR)
	$(EMCC) $(EMCFLAGS) $(COMMON_SRCS) $(GLUE) $(ST16_OBJS) $(SHPX_OBJS) -o $(OUT_DIR)/remix.js \
		-s MODULARIZE=1 \
		-s EXPORT_NAME=createRemixModule \
		-s ALLOW_MEMORY_GROWTH=1 \
		-s INITIAL_MEMORY=67108864 \
		-s STACK_SIZE=5242880 \
		-s EXPORTED_FUNCTIONS='[$(EXPORTS)]' \
		-s EXPORTED_RUNTIME_METHODS='[ccall,cwrap,HEAPU8,FS]' \
		-s ENVIRONMENT=web,worker \
		-s FILESYSTEM=1

remix_st16_host.o: $(REMIX_DIR)/remix_st16_host.cpp $(REMIX_DIR)/remix_st16.h $(REMIX_DIR)/function.h
	$(EMCC) $(EMCXXFLAGS) -c -o $@ $(REMIX_DIR)/remix_st16_host.cpp

remix_st16_stubs.o: $(REMIX_DIR)/remix_st16_stubs.cpp
	$(EMCC) $(EMCXXFLAGS) -c -o $@ $(REMIX_DIR)/remix_st16_stubs.cpp

st16_convert.o: $(ATARILIB)/st16_convert.cpp
	$(EMCC) $(EMCXXFLAGS) -c -o $@ $<

st16_iconset.o: $(ATARILIB)/st16_iconset.cpp
	$(EMCC) $(EMCXXFLAGS) -c -o $@ $<

c2p.o: $(ATARILIB)/c2p.cpp
	$(EMCC) $(EMCXXFLAGS) -c -o $@ $<

remix_shpx_host.o: $(REMIX_DIR)/remix_shpx_host.cpp $(REMIX_DIR)/remix_shpx.h $(REMIX_DIR)/function.h
	$(EMCC) $(EMCXXFLAGS) -c -o $@ $(REMIX_DIR)/remix_shpx_host.cpp

remix_shpx_stubs.o: $(REMIX_DIR)/remix_shpx_stubs.cpp
	$(EMCC) $(EMCXXFLAGS) -c -o $@ $(REMIX_DIR)/remix_shpx_stubs.cpp

keyframe_remix.o: $(REMIX_DIR)/keyframe_remix.cpp $(REMIX_DIR)/function.h
	$(EMCC) $(EMCXXFLAGS) -DATARI_ST -Wno-error -c -o $@ $(REMIX_DIR)/keyframe_remix.cpp

remix_lcw.o: $(REMIX_DIR)/remix_lcw.cpp $(REMIX_DIR)/remix_decode.h
	$(EMCC) $(EMCXXFLAGS) -c -o $@ $(REMIX_DIR)/remix_lcw.cpp

remix_xordelta.o: $(REMIX_DIR)/remix_xordelta.cpp $(REMIX_DIR)/remix_decode.h
	$(EMCC) $(EMCXXFLAGS) -c -o $@ $(REMIX_DIR)/remix_xordelta.cpp

clean:
	rm -f $(OUT_DIR)/remix.js $(OUT_DIR)/remix.wasm $(ST16_OBJS) $(SHPX_OBJS)
