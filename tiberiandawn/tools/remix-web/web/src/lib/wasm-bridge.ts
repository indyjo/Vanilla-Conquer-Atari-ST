/* eslint-disable @typescript-eslint/no-explicit-any */

import { w16StemForTheaterMix } from './theater-st16';
import { shpxPoolBasename, shpxPoolIdForMix } from './shpx';

export interface RemixStats {
  mix_files_ok: number;
  mix_files_error: number;
  mix_files_skipped: number;
  payload_files: number;
  audio_files: number;
  audio_converted: number;
  audio_already_ok: number;
  payload_errors: number;
  iconset_files: number;
  iconset_converted: number;
  iconset_already_st16: number;
  iconset_errors: number;
  shpx_files: number;
  shpx_converted: number;
  shpx_skipped: number;
  shpx_errors: number;
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

export interface RemixMixOptions {
  convertSt16Iconsets?: boolean;
  convertShpx?: boolean;
  mixBasename?: string;
  /** SHPX pool id; defaults from mixBasename when omitted. */
  shpxPoolId?: number;
  w16Bytes?: Uint8Array;
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
  _remix_wasm_set_st16_enabled?: (enabled: number) => void;
  _remix_wasm_set_shpx_enabled?: (enabled: number) => void;
  _remix_wasm_set_mix_basename?: (namePtr: number) => void;
  _remix_wasm_install_w16?: (dataPtr: number, len: number) => number;
  _remix_wasm_memfs_input?: () => number;
  _remix_wasm_cleanup_workfiles?: () => void;
  _remix_wasm_last_entry_count: () => number;
  _remix_wasm_last_entries: () => number;
  _remix_wasm_free: (ptr: number) => void;
  _malloc: (size: number) => number;
  _free: (ptr: number) => void;
  HEAPU8: Uint8Array;
  FS?: {
    writeFile: (path: string, data: Uint8Array | ArrayBuffer) => void;
    readFile: (path: string) => Uint8Array;
    unlink: (path: string) => void;
  };
}

let modulePromise: Promise<RemixModule> | null = null;

const STATS_SIZE = 16 * 4;
const REMIX_ENTRY_SIZE = 84;
const TYPE_FIELD_LEN = 32;
const WASM_MEMFS_INPUT = -1;
const WASM_IN_PATH = '/in.mix';
const WASM_IN_B_PATH = '/in_b.mix';
const WASM_OUT_PATH = '/out.mix';

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

function writeCString(mod: RemixModule, text: string): number {
  const bytes = new TextEncoder().encode(text);
  const ptr = mod._malloc(bytes.length + 1);
  mod.HEAPU8.set(bytes, ptr);
  mod.HEAPU8[ptr + bytes.length] = 0;
  return ptr;
}

function requireMemfs(mod: RemixModule): NonNullable<RemixModule['FS']> {
  if (!mod.FS) {
    throw new Error('remix.wasm missing MEMFS — rebuild remix.wasm');
  }
  return mod.FS;
}

function memfsInputFlag(mod: RemixModule): number {
  if (typeof mod._remix_wasm_memfs_input === 'function') {
    return mod._remix_wasm_memfs_input();
  }
  return WASM_MEMFS_INPUT;
}

function writeMemfsFile(mod: RemixModule, path: string, data: Uint8Array): void {
  const fs = requireMemfs(mod);
  try {
    fs.unlink(path);
  } catch {
    // not present yet
  }
  fs.writeFile(path, data);
}

function readMemfsFile(mod: RemixModule, path: string): Uint8Array {
  return requireMemfs(mod).readFile(path);
}

function resolveShpxPoolId(options?: RemixMixOptions): number {
  if (options?.shpxPoolId && options.shpxPoolId > 0) return options.shpxPoolId;
  if (options?.mixBasename) {
    const fromName = shpxPoolIdForMix(options.mixBasename);
    if (fromName) return fromName;
  }
  return 1;
}

function clearShpxPool(mod: RemixModule, poolId: number): void {
  if (!mod.FS) return;
  try {
    mod.FS.unlink(shpxPoolBasename(poolId));
  } catch {
    // no prior pool file
  }
}

function readShpxPool(mod: RemixModule, poolId: number): Uint8Array | undefined {
  if (!mod.FS) return undefined;
  try {
    const data = mod.FS.readFile(shpxPoolBasename(poolId));
    return data.length > 0 ? data : undefined;
  } catch {
    return undefined;
  }
}

function configureRemixModule(mod: RemixModule, options?: RemixMixOptions): void {
  const wantSt16 = Boolean(options?.convertSt16Iconsets);
  const wantShpx = Boolean(options?.convertShpx);
  const shpxPoolId = resolveShpxPoolId(options);
  if (typeof mod._remix_wasm_set_st16_enabled !== 'function') {
    if (wantSt16) {
      throw new Error(
        'ST16 iconset conversion is not available in this remix-web build — rebuild remix.wasm',
      );
    }
  } else {
    mod._remix_wasm_set_st16_enabled(wantSt16 ? 1 : 0);
  }

  if (typeof mod._remix_wasm_set_shpx_enabled !== 'function') {
    if (wantShpx) {
      throw new Error(
        'SHPX conversion is not available in this remix-web build — rebuild remix.wasm',
      );
    }
  } else {
    mod._remix_wasm_set_shpx_enabled(wantShpx ? 1 : 0);
    if (wantShpx) {
      clearShpxPool(mod, shpxPoolId);
    }
  }

  if (options?.mixBasename && mod._remix_wasm_set_mix_basename) {
    const namePtr = writeCString(mod, options.mixBasename);
    try {
      mod._remix_wasm_set_mix_basename(namePtr);
    } finally {
      mod._free(namePtr);
    }
  }

  if (wantSt16 && options?.w16Bytes) {
    if (options.mixBasename && mod.FS) {
      const stem = w16StemForTheaterMix(options.mixBasename);
      if (stem) {
        /* MEMFS is case-sensitive; release ZIP uses lowercase *.w16 names. */
        mod.FS.writeFile(`${stem.toLowerCase()}.w16`, options.w16Bytes);
      }
    }
    if (!mod._remix_wasm_install_w16) {
      throw new Error('remix.wasm missing remix_wasm_install_w16');
    }
    const w16Ptr = mod._malloc(options.w16Bytes.length);
    try {
      mod.HEAPU8.set(options.w16Bytes, w16Ptr);
      const ok = mod._remix_wasm_install_w16(w16Ptr, options.w16Bytes.length);
      if (!ok) {
        throw new Error('Failed to install C2P weights from release .W16');
      }
    } finally {
      mod._free(w16Ptr);
    }
  }
}

function wasmHeap(mod: RemixModule): Uint8Array {
  return mod.HEAPU8;
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
  const heap = wasmHeap(mod);
  const statsView = new DataView(heap.buffer, heap.byteOffset + statsPtr, STATS_SIZE);
  return {
    mix_files_ok: statsView.getUint32(0, true),
    mix_files_error: statsView.getUint32(4, true),
    mix_files_skipped: statsView.getUint32(8, true),
    payload_files: statsView.getUint32(12, true),
    audio_files: statsView.getUint32(16, true),
    audio_converted: statsView.getUint32(20, true),
    audio_already_ok: statsView.getUint32(24, true),
    payload_errors: statsView.getUint32(28, true),
    iconset_files: statsView.getUint32(32, true),
    iconset_converted: statsView.getUint32(36, true),
    iconset_already_st16: statsView.getUint32(40, true),
    iconset_errors: statsView.getUint32(44, true),
    shpx_files: statsView.getUint32(48, true),
    shpx_converted: statsView.getUint32(52, true),
    shpx_skipped: statsView.getUint32(56, true),
    shpx_errors: statsView.getUint32(60, true),
  };
}

function readWasmEntries(mod: RemixModule): RemixEntry[] {
  const count = mod._remix_wasm_last_entry_count();
  if (count === 0) {
    return [];
  }
  const basePtr = mod._remix_wasm_last_entries();
  if (!basePtr) {
    return [];
  }
  const entries: RemixEntry[] = [];
  const heap = wasmHeap(mod);
  const heapEnd = heap.byteOffset + heap.byteLength;

  for (let i = 0; i < count; i++) {
    const off = basePtr + i * REMIX_ENTRY_SIZE;
    if (off + REMIX_ENTRY_SIZE > heapEnd) {
      break;
    }
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

function cleanupWorkfiles(mod: RemixModule): void {
  mod._remix_wasm_cleanup_workfiles?.();
}

function readOutput(mod: RemixModule): Uint8Array {
  return readMemfsFile(mod, WASM_OUT_PATH);
}

export async function remixMixBytes(
  input: Uint8Array,
  baseUrl = '/',
  options?: RemixMixOptions,
): Promise<{
  output: Uint8Array;
  stats: RemixStats;
  entries: RemixEntry[];
  shpxPool?: Uint8Array;
}> {
  const mod = await loadRemixModule(baseUrl);
  configureRemixModule(mod, options);
  writeMemfsFile(mod, WASM_IN_PATH, input);
  const memfsInput = memfsInputFlag(mod);
  const statsPtr = mod._malloc(STATS_SIZE);

  try {
    mod.HEAPU8.fill(0, statsPtr, statsPtr + STATS_SIZE);

    const rc = mod._remix_wasm_process_mix(
      0,
      memfsInput,
      0,
      0,
      statsPtr,
    );

    const stats = readStats(mod, statsPtr);
    const entries = readWasmEntries(mod);
    if (rc <= 0) {
      let detail = '';
      if (stats.iconset_errors > 0) {
        detail = ` (${stats.iconset_errors} iconset conversion error(s))`;
      } else if (stats.shpx_errors > 0) {
        detail = ` (${stats.shpx_errors} SHPX conversion error(s))`;
      } else if (options?.convertSt16Iconsets && options?.mixBasename) {
        detail = ` (ST16 enabled for ${options.mixBasename} — see browser devtools console)`;
      }
      throw new Error(`remix_wasm_process_mix failed (rc=${rc})${detail}`);
    }

    const output = readOutput(mod);
    const shpxPool = options?.convertShpx
      ? readShpxPool(mod, resolveShpxPoolId(options))
      : undefined;
    return { output, stats, entries, shpxPool };
  } finally {
    mod._free(statsPtr);
    cleanupWorkfiles(mod);
  }
}

export async function remixMergeMixBytes(
  inputA: Uint8Array,
  inputB: Uint8Array,
  baseUrl = '/',
  options?: RemixMixOptions,
): Promise<{ output: Uint8Array; stats: RemixStats; entries: RemixEntry[]; shpxPool?: Uint8Array }> {
  const mod = await loadRemixModule(baseUrl);
  configureRemixModule(mod, options);
  writeMemfsFile(mod, WASM_IN_PATH, inputA);
  writeMemfsFile(mod, WASM_IN_B_PATH, inputB);
  const memfsInput = memfsInputFlag(mod);
  const statsPtr = mod._malloc(STATS_SIZE);

  try {
    mod.HEAPU8.fill(0, statsPtr, statsPtr + STATS_SIZE);

    const rc = mod._remix_wasm_merge_and_process_mix(
      0,
      memfsInput,
      0,
      memfsInput,
      0,
      0,
      statsPtr,
    );

    const stats = readStats(mod, statsPtr);
    const entries = readWasmEntries(mod);
    if (rc <= 0) {
      throw new Error(`remix_wasm_merge_and_process_mix failed (rc=${rc})`);
    }

    const output = readOutput(mod);
    const shpxPool = options?.convertShpx
      ? readShpxPool(mod, resolveShpxPoolId(options))
      : undefined;
    return { output, stats, entries, shpxPool };
  } finally {
    mod._free(statsPtr);
    cleanupWorkfiles(mod);
  }
}
