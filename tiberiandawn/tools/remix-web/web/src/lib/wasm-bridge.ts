/* eslint-disable @typescript-eslint/no-explicit-any */

export interface RemixStats {
  mix_files_ok: number;
  mix_files_error: number;
  mix_files_skipped: number;
  payload_files: number;
  audio_files: number;
  audio_converted: number;
  audio_already_ok: number;
  payload_errors: number;
}

/** Layout of RemixEntry in WASM memory (must match remix.h). */
export interface RemixEntry {
  crc: number;
  oldOffset: number;
  oldSize: number;
  newOffset: number;
  newSize: number;
  typeIn: string;
  typeOut: string;
}

export interface RemixModule {
  _remix_wasm_process_mix: (
    inPtr: number,
    inLen: number,
    outPtrPtr: number,
    outLenPtr: number,
    statsPtr: number,
  ) => number;
  _remix_wasm_merge_and_process_mix: (
    inAPtr: number,
    inALen: number,
    inBPtr: number,
    inBLen: number,
    outPtrPtr: number,
    outLenPtr: number,
    statsPtr: number,
  ) => number;
  _remix_wasm_last_entry_count: () => number;
  _remix_wasm_last_entries: () => number;
  _remix_wasm_free: (ptr: number) => void;
  _malloc: (size: number) => number;
  _free: (ptr: number) => void;
  HEAPU8: Uint8Array;
}

let modulePromise: Promise<RemixModule> | null = null;

const STATS_SIZE = 8 * 4;
const REMIX_ENTRY_SIZE = 84;
const TYPE_FIELD_LEN = 32;

/** Vite BASE_URL is "/" — resolve against the page origin for URL(). */
function resolveBaseUrl(baseUrl: string): string {
  return new URL(baseUrl, window.location.href).href;
}

function assetUrl(path: string, baseUrl: string): string {
  return new URL(path, resolveBaseUrl(baseUrl)).href;
}

function remixScriptUrl(baseUrl: string): string {
  return assetUrl('remix.js', baseUrl);
}

function loadRemixScript(baseUrl: string): Promise<void> {
  const url = remixScriptUrl(baseUrl);
  return new Promise((resolve, reject) => {
    const existing = document.querySelector(`script[data-remix-wasm="${url}"]`);
    if (existing) {
      resolve();
      return;
    }
    const script = document.createElement('script');
    script.src = url;
    script.async = true;
    script.dataset.remixWasm = url;
    script.onload = () => resolve();
    script.onerror = () => reject(new Error(`Failed to load ${url}`));
    document.head.appendChild(script);
  });
}

declare global {
  interface Window {
    createRemixModule?: (opts: {
      locateFile: (path: string) => string;
    }) => Promise<RemixModule>;
  }
  // Emscripten MODULARIZE assigns a global var when loaded via <script>.
  var createRemixModule:
    | ((opts: { locateFile: (path: string) => string }) => Promise<RemixModule>)
    | undefined;
}

export function loadRemixModule(baseUrl = '/'): Promise<RemixModule> {
  if (!modulePromise) {
    modulePromise = (async () => {
      await loadRemixScript(baseUrl);
      const createRemixModule =
        typeof globalThis.createRemixModule === 'function'
          ? globalThis.createRemixModule
          : window.createRemixModule;
      if (typeof createRemixModule !== 'function') {
        throw new Error('createRemixModule not found after loading remix.js');
      }
      return createRemixModule({
        locateFile: (path: string) => assetUrl(path, baseUrl),
      });
    })().catch((err) => {
      modulePromise = null;
      throw err;
    });
  }
  return modulePromise;
}

function readCString(heap: Uint8Array, offset: number, maxLen: number): string {
  let end = offset;
  const limit = offset + maxLen;
  while (end < limit && heap[end] !== 0) {
    end++;
  }
  return new TextDecoder().decode(heap.subarray(offset, end));
}

function readStats(mod: RemixModule, statsPtr: number): RemixStats {
  const statsView = new DataView(mod.HEAPU8.buffer, statsPtr, STATS_SIZE);
  return {
    mix_files_ok: statsView.getUint32(0, true),
    mix_files_error: statsView.getUint32(4, true),
    mix_files_skipped: statsView.getUint32(8, true),
    payload_files: statsView.getUint32(12, true),
    audio_files: statsView.getUint32(16, true),
    audio_converted: statsView.getUint32(20, true),
    audio_already_ok: statsView.getUint32(24, true),
    payload_errors: statsView.getUint32(28, true),
  };
}

function readWasmEntries(mod: RemixModule): RemixEntry[] {
  const count = mod._remix_wasm_last_entry_count();
  if (count === 0) {
    return [];
  }
  const basePtr = mod._remix_wasm_last_entries();
  const entries: RemixEntry[] = [];
  const heap = mod.HEAPU8;

  for (let i = 0; i < count; i++) {
    const off = basePtr + i * REMIX_ENTRY_SIZE;
    const view = new DataView(heap.buffer, heap.byteOffset + off, REMIX_ENTRY_SIZE);
    entries.push({
      crc: view.getUint32(0, true),
      oldOffset: view.getUint32(4, true),
      oldSize: view.getUint32(8, true),
      newOffset: view.getUint32(12, true),
      newSize: view.getUint32(16, true),
      typeIn: readCString(heap, off + 20, TYPE_FIELD_LEN),
      typeOut: readCString(heap, off + 52, TYPE_FIELD_LEN),
    });
  }
  return entries;
}

function readOutput(mod: RemixModule, outPtrPtr: number, outLenPtr: number): Uint8Array {
  const heapView = new DataView(mod.HEAPU8.buffer);
  const outDataPtr = heapView.getUint32(outPtrPtr, true);
  const outLen = heapView.getUint32(outLenPtr, true);
  const output = mod.HEAPU8.slice(outDataPtr, outDataPtr + outLen);
  mod._remix_wasm_free(outDataPtr);
  return output;
}

export async function remixMixBytes(input: Uint8Array, baseUrl = '/'): Promise<{
  output: Uint8Array;
  stats: RemixStats;
  entries: RemixEntry[];
}> {
  const mod = await loadRemixModule(baseUrl);
  const inPtr = mod._malloc(input.length);
  const outPtrPtr = mod._malloc(4);
  const outLenPtr = mod._malloc(4);
  const statsPtr = mod._malloc(STATS_SIZE);

  try {
    mod.HEAPU8.set(input, inPtr);
    mod.HEAPU8.fill(0, statsPtr, statsPtr + STATS_SIZE);

    const rc = mod._remix_wasm_process_mix(
      inPtr,
      input.length,
      outPtrPtr,
      outLenPtr,
      statsPtr,
    );

    const stats = readStats(mod, statsPtr);
    const entries = readWasmEntries(mod);
    if (rc <= 0) {
      throw new Error(`remix_wasm_process_mix failed (rc=${rc})`);
    }

    const output = readOutput(mod, outPtrPtr, outLenPtr);
    return { output, stats, entries };
  } finally {
    mod._free(inPtr);
    mod._free(outPtrPtr);
    mod._free(outLenPtr);
    mod._free(statsPtr);
  }
}

export async function remixMergeMixBytes(
  inputA: Uint8Array,
  inputB: Uint8Array,
  baseUrl = '/',
): Promise<{ output: Uint8Array; stats: RemixStats; entries: RemixEntry[] }> {
  const mod = await loadRemixModule(baseUrl);
  const inAPtr = mod._malloc(inputA.length);
  const inBPtr = mod._malloc(inputB.length);
  const outPtrPtr = mod._malloc(4);
  const outLenPtr = mod._malloc(4);
  const statsPtr = mod._malloc(STATS_SIZE);

  try {
    mod.HEAPU8.set(inputA, inAPtr);
    mod.HEAPU8.set(inputB, inBPtr);
    mod.HEAPU8.fill(0, statsPtr, statsPtr + STATS_SIZE);

    const rc = mod._remix_wasm_merge_and_process_mix(
      inAPtr,
      inputA.length,
      inBPtr,
      inputB.length,
      outPtrPtr,
      outLenPtr,
      statsPtr,
    );

    const stats = readStats(mod, statsPtr);
    const entries = readWasmEntries(mod);
    if (rc <= 0) {
      throw new Error(`remix_wasm_merge_and_process_mix failed (rc=${rc})`);
    }

    const output = readOutput(mod, outPtrPtr, outLenPtr);
    return { output, stats, entries };
  } finally {
    mod._free(inAPtr);
    mod._free(inBPtr);
    mod._free(outPtrPtr);
    mod._free(outLenPtr);
    mod._free(statsPtr);
  }
}
