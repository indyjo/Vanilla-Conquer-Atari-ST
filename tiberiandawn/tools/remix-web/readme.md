# Remix Web

Browser-based wizard that extracts MIX files from a Command & Conquer for the Atari ST install disc and repacks them with [REMIX](../remix/). All processing runs locally — no game data is uploaded or hosted.

## Prerequisites

- **Node.js 18+** and npm
- **Emscripten** at `~/emsdk` (same as other WASM tooling in this repo)
- Your own **GDI and NOD** C&amp;C DOS CD install discs (volume labels must read `GDI` and `NOD`)

## Build

From the `tiberiandawn` directory:

```bash
make remix-web
```

This compiles REMIX to WebAssembly and builds the static site into `tools/remix-web/web/dist/`.

**Note:** `make remix-web` saves your `PATH` before activating Emscripten so npm uses your system Node (18+), not emsdk’s bundled Node 14.

Asset paths are relative (`base: './'`) so the build works on itch.io and other static hosts.

### itch.io upload

```bash
make remix-web-itch
```

Creates `tools/remix-web/dist/remix-web-itch.zip` — upload to a new **HTML** project on itch.io and enable **This file will be played in the browser**. The zip root contains `index.html`, `remix.js`, `remix.wasm`, and `assets/`.

Test locally before uploading:

```bash
cd tools/remix-web/web/dist && python3 -m http.server 8080
```

### Development

```bash
# Terminal 1 — WASM (once, or after remix/ changes)
make -C tools/remix-web/wasm

# Terminal 2 — Vite dev server
cd tools/remix-web/web && npm install && npm run dev
```

Open the URL printed by Vite. Place `remix.js` / `remix.wasm` in `web/public/` (the wasm Makefile does this).

## Usage

1. **Discs** — pick **both** GDI and NOD install media (ISO or ZIP with one disc image); optionally the [itch.io release ZIP](https://indyjo.itch.io/commandconquer) for `cnc.tos` + `*.w16`
2. **Customize** — toggle optional speech/SFX and music (`SCORES.MIX`)
3. **Process** — streaming ISO extract → merge `GENERAL.MIX` when dual-disc → REMIX each MIX → bundle release files if provided
4. **Checkout** — download ZIP (MIX-only, or full ready-to-play folder if release ZIP was attached)

Copy the ZIP contents to a folder on your Atari ST drive. If you skipped the release ZIP, add `cnc.tos` and `*.W16` from itch.io manually.

## Legal

You must legally own Command &amp; Conquer. This tool does not distribute Electronic Arts assets.

## Roadmap

- **Phase 4:** CI golden test (host remix == WASM), cancel pipeline, memory warnings
