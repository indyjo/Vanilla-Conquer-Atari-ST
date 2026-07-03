# Remix Web

Browser-based wizard that extracts MIX files from a Command & Conquer for the Atari ST install disc and repacks them with [REMIX](../remix/). All processing runs locally — no game data is uploaded or hosted.

## Prerequisites

- **Node.js 18+** and npm
- **Emscripten** at `~/emsdk` (same as other WASM tooling in this repo)
- Your own **GDI and NOD** C&amp;C DOS CD install discs (volume labels must read `GDI` and `NOD`)

## Build

From `tools/remix-web/`:

```bash
make
```

Or from the `tiberiandawn` directory:

```bash
make remix-web
```

Both compile REMIX to WebAssembly and build the static site into `web/dist/`.

**Note:** the build saves your `PATH` before activating Emscripten so npm uses your system Node (18+), not emsdk’s bundled Node 14.

Other targets (run from `tools/remix-web/`):

```bash
make wasm    # remix.wasm only
make web     # Vite build (WASM must already be in web/public/)
make itch    # production build + dist/remix-web-itch.zip
make clean
```

Asset paths are relative (`base: './'`) so the build works on itch.io and other static hosts.

### itch.io upload

From `tools/remix-web/`:

```bash
make itch
```

Or from `tiberiandawn/`:

```bash
make remix-web-itch
```

Creates `dist/remix-web-itch.zip` — upload to a new **HTML** project on itch.io and enable **This file will be played in the browser**. The zip root contains `index.html`, `remix.js`, `remix.wasm`, and `assets/`.

Test locally before uploading:

```bash
cd tools/remix-web/web/dist && python3 -m http.server 8080
```

### Development

```bash
# Terminal 1 — WASM (once, or after remix/ changes)
make wasm

# Terminal 2 — Vite dev server
cd web && npm install && npm run dev
```

Open the URL printed by Vite. Place `remix.js` / `remix.wasm` in `web/public/` (the wasm Makefile does this).

## Usage

1. **Discs** — pick **both** GDI and NOD install media (ISO or ZIP with one disc image); optionally the [itch.io release ZIP](https://indyjo.itch.io/commandconquer) for `cnc.tos` + `*.w16`
2. **Customize** — target C&C4ST version (0.1.x / 0.2.x), optional ST16 iconset conversion for theater MIX files, optional SHPX shape conversion for CONQUER.MIX, speech/SFX and music toggles
3. **Process** — streaming ISO extract → merge `GENERAL.MIX` when dual-disc → REMIX each MIX → bundle release files if provided
4. **Checkout** — download ZIP (MIX-only, or full ready-to-play folder if release ZIP was attached)

Copy the ZIP contents to a folder on your Atari ST drive. If you skipped the release ZIP, add `cnc.tos` and `*.W16` from itch.io manually.

### ST16 iconsets (0.2.x)

When **Convert terrain iconsets to ST16** is enabled (default for target **0.2.x**), remix-web pre-converts iconsets in theater MIX files (`TEMPERAT`, `DESERT`, `WINTER`, `SNOW`, `JUNGLE`). This requires the itch.io release ZIP (for matching `*.W16` weights). Target **0.1.x** disables ST16 by default; enabling it shows an incompatibility warning.

### SHPX shapes (0.2.x)

When **Convert shapes to SHPX** is enabled (default for target **0.2.x**), remix-web converts KeyFrame SHPs in `CONQUER.MIX` to the external-pool SHPX format and writes `pool0001.bin` into the output ZIP. This saves RAM on the Atari ST and is mandatory on 4 MB machines. The feature is experimental and incompatible with the **0.1.x** line of C&C4ST.

## Legal

You must legally own Command &amp; Conquer. This tool does not distribute Electronic Arts assets.

## Roadmap

- **Phase 4:** CI golden test (host remix == WASM), cancel pipeline, memory warnings
