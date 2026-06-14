# remix-web WASM — compile remix/ sources with Emscripten.
# Requires: source ~/emsdk/emsdk_env.sh  (or use top-level make remix-web)

REMIX_DIR = ../../remix
OUT_DIR = ../web/public

EMCC ?= emcc
EMCFLAGS = -O2 -Wall -Wextra -std=c99 -I$(REMIX_DIR)

COMMON_SRCS = \
	$(REMIX_DIR)/remix_mix.c \
	$(REMIX_DIR)/remix_merge.c \
	$(REMIX_DIR)/remix_detect.c \
	$(REMIX_DIR)/remix_print.c \
	$(REMIX_DIR)/remix_audio.c \
	$(REMIX_DIR)/remix_aud.c \
	$(REMIX_DIR)/remix_unzap.c

GLUE = remix_wasm.c

EXPORTS = _remix_wasm_process_mix,_remix_wasm_merge_and_process_mix,_remix_wasm_free,_remix_wasm_last_entry_count,_remix_wasm_last_entries,_malloc,_free

.PHONY: all clean

all: $(OUT_DIR)/remix.js $(OUT_DIR)/remix.wasm

$(OUT_DIR)/remix.js $(OUT_DIR)/remix.wasm: $(COMMON_SRCS) $(GLUE)
	@mkdir -p $(OUT_DIR)
	$(EMCC) $(EMCFLAGS) $(COMMON_SRCS) $(GLUE) -o $(OUT_DIR)/remix.js \
		-s MODULARIZE=1 \
		-s EXPORT_NAME=createRemixModule \
		-s ALLOW_MEMORY_GROWTH=1 \
		-s EXPORTED_FUNCTIONS='[$(EXPORTS)]' \
		-s EXPORTED_RUNTIME_METHODS='[ccall,cwrap,HEAPU8]' \
		-s ENVIRONMENT=web,worker \
		-s FILESYSTEM=1

clean:
	rm -f $(OUT_DIR)/remix.js $(OUT_DIR)/remix.wasm
