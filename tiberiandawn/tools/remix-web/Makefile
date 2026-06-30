# remix-web — browser MIX prep wizard (WASM + static site).
#
#   make              full build (WASM + Vite)
#   make wasm         compile remix.wasm only
#   make web          Vite production build (requires up-to-date WASM in web/public/)
#   make itch         build + itch.io upload zip
#   make clean        remove build outputs

WEB_DIR = web
WASM_DIR = wasm
DIST_DIR = dist
ITCH_ZIP = $(DIST_DIR)/remix-web-itch.zip
EMSDK_ENV = $(HOME)/emsdk/emsdk_env.sh

.PHONY: all build wasm web itch clean help

all: build

build: wasm web

wasm:
	@set -e; \
	if [ -f "$(EMSDK_ENV)" ]; then \
		. "$(EMSDK_ENV)"; \
	else \
		echo "error: $(EMSDK_ENV) not found" >&2; \
		exit 1; \
	fi; \
	$(MAKE) -C $(WASM_DIR)

web:
	@set -e; \
	SAVED_PATH="$$PATH"; \
	if [ -f "$(EMSDK_ENV)" ]; then . "$(EMSDK_ENV)"; fi; \
	cd $(WEB_DIR) && PATH="$$SAVED_PATH" npm ci && PATH="$$SAVED_PATH" npm run build; \
	echo "remix-web built: $(WEB_DIR)/dist/"

itch: build
	@set -e; \
	if [ ! -f $(WEB_DIR)/dist/index.html ]; then \
		echo "error: $(WEB_DIR)/dist/index.html not found" >&2; \
		exit 1; \
	fi; \
	mkdir -p $(DIST_DIR); \
	rm -f "$(ITCH_ZIP)"; \
	( cd $(WEB_DIR)/dist && zip -q -r "../../$(ITCH_ZIP)" . ); \
	echo "Created $(ITCH_ZIP) (upload to itch.io as HTML)"

clean:
	$(MAKE) -C $(WASM_DIR) clean
	rm -rf $(WEB_DIR)/dist
	rm -f "$(ITCH_ZIP)"

help:
	@echo "remix-web targets:"
	@echo "  make        WASM + static site ($(WEB_DIR)/dist/)"
	@echo "  make wasm   compile remix.wasm -> $(WEB_DIR)/public/"
	@echo "  make web    npm ci && vite build"
	@echo "  make itch   build + $(ITCH_ZIP)"
	@echo "  make clean  remove dist, itch zip, and WASM outputs"
