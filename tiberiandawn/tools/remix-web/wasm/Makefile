# remix-web WASM — compile remix/ sources with Emscripten.
# Requires: source ~/emsdk/emsdk_env.sh  (or: make -C .. wasm from remix-web/)

REMIX_DIR = ../../remix
TDROOT = ../../..
ATARILIB = $(TDROOT)/atarilib
OUT_DIR = ../web/public

EMCC ?= emcc
EMCFLAGS = -O2 -Wall -Wextra -std=c99 -I$(REMIX_DIR) -I$(TDROOT) -I$(ATARILIB)
EMCXXFLAGS = -O2 -Wall -Wextra -std=c++17 -I$(REMIX_DIR) -I$(ATARILIB) -I$(TDROOT) -DREMIX_BUILD

COMMON_SRCS = \
	$(REMIX_DIR)/remix_mix.c \
	$(REMIX_DIR)/remix_merge.c \
	$(REMIX_DIR)/remix_detect.c \
	$(REMIX_DIR)/remix_print.c \
	$(REMIX_DIR)/remix_audio.c \
	$(REMIX_DIR)/remix_aud.c \
	$(REMIX_DIR)/remix_unzap.c

ST16_OBJS = remix_st16_host.o remix_st16_stubs.o st16_convert.o st16_iconset.o c2p.o

GLUE = remix_wasm.c

EXPORTS = _remix_wasm_process_mix,_remix_wasm_merge_and_process_mix,_remix_wasm_free,_remix_wasm_last_entry_count,_remix_wasm_last_entries,_remix_wasm_set_st16_enabled,_remix_wasm_set_mix_basename,_remix_wasm_install_w16,_malloc,_free

.PHONY: all clean

all: $(OUT_DIR)/remix.js $(OUT_DIR)/remix.wasm

$(OUT_DIR)/remix.js $(OUT_DIR)/remix.wasm: $(COMMON_SRCS) $(ST16_OBJS) $(GLUE)
	@mkdir -p $(OUT_DIR)
	$(EMCC) $(EMCFLAGS) $(COMMON_SRCS) $(GLUE) $(ST16_OBJS) -o $(OUT_DIR)/remix.js \
		-s MODULARIZE=1 \
		-s EXPORT_NAME=createRemixModule \
		-s ALLOW_MEMORY_GROWTH=1 \
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

clean:
	rm -f $(OUT_DIR)/remix.js $(OUT_DIR)/remix.wasm $(ST16_OBJS)
