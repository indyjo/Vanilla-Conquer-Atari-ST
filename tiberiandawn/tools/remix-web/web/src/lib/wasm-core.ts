/* eslint-disable @typescript-eslint/no-explicit-any */

import { w16StemForTheaterMix } from './theater-st16';
import { audxPoolBasename, audxPoolIdForMix } from './audx';
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
  audx_files: number;
  audx_converted: number;
  audx_errors: number;
  vqa_files: number;
  vqa_converted: number;
  vqa_omitted: number;
  vqa_already_stv: number;
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
  omit: number;
}

export interface RemixMixOptions {
  convertSt16Iconsets?: boolean;
  convertShpx?: boolean;
  convertAudx?: boolean;
  convertVqa?: boolean;
  videoQuality?: 'low' | 'medium' | 'high';
  videoEffort?: 'fast' | 'normal' | 'thorough';
  /** Parallel encode workers for MOVIES.MIX (1–16). */
  videoParallelism?: number;
  mixBasename?: string;
  /** SHPX pool id; defaults from mixBasename when omitted. */
  shpxPoolId?: number;
  /** AUDX pool id; defaults from mixBasename when omitted. */
  audxPoolId?: number;
  w16Bytes?: Uint8Array;
  /** CRC-named FMV sidecars keyed as `video/xxxxxxxx.n.w16`. */
  videoW16Files?: [string, Uint8Array][];
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
  _remix_wasm_encode_vqa?: (crc: number) => number;
  _remix_wasm_set_st16_enabled?: (enabled: number) => void;
  _remix_wasm_set_shpx_enabled?: (enabled: number) => void;
  _remix_wasm_set_audx_enabled?: (enabled: number) => void;
  _remix_wasm_set_vqa_enabled?: (enabled: number) => void;
  _remix_wasm_set_video_quality?: (quality: number) => void;
  _remix_wasm_set_video_effort?: (effort: number) => void;
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
  /** Set by JS; called from WASM during VQA→STVQ encode. */
  onRemixProgress?: (phase: string, crc: number, done: number, total: number) => void;
  FS?: {
    writeFile: (path: string, data: Uint8Array | ArrayBuffer) => void;
    readFile: (path: string) => Uint8Array;
    unlink: (path: string) => void;
    mkdir: (path: string) => void;
    readdir?: (path: string) => string[];
  };
}

/** Per-VQA encode progress from remix.wasm (via worker). */
export type RemixEncodeProgress = {
  /** "start" = encode beginning (done = VQA size); "prep"/"encode" = frame progress; "done" = finished. */
  phase: 'start' | 'prep' | 'encode' | 'done' | string;
  crc: number;
  done: number;
  total: number;
};

export type RemixProgressHandler = (progress: RemixEncodeProgress) => void;

let modulePromise: Promise<RemixModule> | null = null;
let activeProgressHandler: RemixProgressHandler | null = null;

function attachProgressHandler(mod: RemixModule): void {
  mod.onRemixProgress = (phase, crc, done, total) => {
    activeProgressHandler?.({ phase, crc, done, total });
  };
}

/* Must match RemixStats in remix.h (23 × uint32). */
const STATS_SIZE = 23 * 4;
const REMIX_ENTRY_SIZE = 88;
const TYPE_FIELD_LEN = 32;
const WASM_MEMFS_INPUT = -1;
const WASM_IN_PATH = '/in.mix';
const WASM_IN_B_PATH = '/in_b.mix';
const WASM_OUT_PATH = '/out.mix';
const WASM_IN_VQA_PATH = '/in.vqa';
const WASM_OUT_STV_PATH = '/out.stv';

function videoQualityToInt(q: 'low' | 'medium' | 'high' | undefined): number {
  if (q === 'low') return 0;
  if (q === 'high') return 2;
  return 1;
}

function videoEffortToInt(e: 'fast' | 'normal' | 'thorough' | undefined): number {
  if (e === 'fast') return 0;
  if (e === 'thorough') return 2;
  return 1;
}

declare global {
  // eslint-disable-next-line no-var
  var createRemixModule:
    | ((opts: { locateFile: (path: string) => string }) => Promise<RemixModule>)
    | undefined;
}

/** Prefer an absolute baseUrl from the main thread; fall back to self.location. */
function resolveBaseUrl(baseUrl: string): string {
  if (/^https?:\/\//i.test(baseUrl) || baseUrl.startsWith('blob:')) {
    return baseUrl.endsWith('/') ? baseUrl : `${baseUrl.replace(/\/?$/, '/')}`;
  }
  const origin =
    typeof self !== 'undefined' && 'location' in self && self.location?.href
      ? self.location.href
      : 'http://localhost/';
  return new URL(baseUrl, origin).href;
}

function assetUrl(path: string, baseUrl: string): string {
  return new URL(path, resolveBaseUrl(baseUrl)).href;
}

function remixScriptUrl(baseUrl: string): string {
  return assetUrl('remix.js', baseUrl);
}

/**
 * Load Emscripten glue (classic MODULARIZE IIFE) into globalThis.createRemixModule.
 *
 * Vite workers are always ES modules in dev — importScripts() exists but throws — so we
 * fetch the script and evaluate it in a non-module Function scope instead.
 */
async function loadRemixScript(baseUrl: string): Promise<void> {
  if (typeof globalThis.createRemixModule === 'function') return;

  const url = remixScriptUrl(baseUrl);
  const res = await fetch(url);
  if (!res.ok) {
    throw new Error(`Failed to load ${url} (${res.status})`);
  }
  const code = await res.text();
  // remix.js is `var createRemixModule = (() => { ... })();` — evaluate outside module scope.
  // eslint-disable-next-line @typescript-eslint/no-implied-eval
  const factory = new Function(
    `${code}\n; return typeof createRemixModule === "function" ? createRemixModule : undefined;`,
  );
  const create = factory();
  if (typeof create !== 'function') {
    throw new Error(`createRemixModule not found after evaluating ${url}`);
  }
  globalThis.createRemixModule = create;
}

export function loadRemixModule(baseUrl = '/'): Promise<RemixModule> {
  if (!modulePromise) {
    modulePromise = (async () => {
      await loadRemixScript(baseUrl);
      const createRemixModule = globalThis.createRemixModule;
      if (typeof createRemixModule !== 'function') {
        throw new Error('createRemixModule not found after loading remix.js');
      }
      const mod = await createRemixModule({
        locateFile: (path: string) => assetUrl(path, baseUrl),
      });
      attachProgressHandler(mod);
      return mod;
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

function resolveAudxPoolId(options?: RemixMixOptions): number {
  if (options?.audxPoolId && options.audxPoolId > 0) return options.audxPoolId;
  if (options?.mixBasename) {
    const fromName = audxPoolIdForMix(options.mixBasename);
    if (fromName) return fromName;
  }
  return 5;
}

function clearAudxPool(mod: RemixModule, poolId: number): void {
  if (!mod.FS) return;
  try {
    mod.FS.unlink(audxPoolBasename(poolId));
  } catch {
    // no prior pool file
  }
}

function readAudxPool(mod: RemixModule, poolId: number): Uint8Array | undefined {
  if (!mod.FS) return undefined;
  try {
    const data = mod.FS.readFile(audxPoolBasename(poolId));
    return data.length > 0 ? data : undefined;
  } catch {
    return undefined;
  }
}

function configureRemixModule(mod: RemixModule, options?: RemixMixOptions): void {
  const wantSt16 = Boolean(options?.convertSt16Iconsets);
  const wantShpx = Boolean(options?.convertShpx);
  const wantAudx = Boolean(options?.convertAudx);
  const wantVqa = Boolean(options?.convertVqa);
  const shpxPoolId = resolveShpxPoolId(options);
  const audxPoolId = resolveAudxPoolId(options);
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

  if (typeof mod._remix_wasm_set_audx_enabled !== 'function') {
    if (wantAudx) {
      throw new Error(
        'AUDX conversion is not available in this remix-web build — rebuild remix.wasm',
      );
    }
  } else {
    mod._remix_wasm_set_audx_enabled(wantAudx ? 1 : 0);
    if (wantAudx) {
      clearAudxPool(mod, audxPoolId);
    }
  }

  if (typeof mod._remix_wasm_set_vqa_enabled !== 'function') {
    if (wantVqa) {
      throw new Error(
        'VQA→STVQ conversion is not available in this remix-web build — rebuild remix.wasm',
      );
    }
  } else {
    mod._remix_wasm_set_vqa_enabled(wantVqa ? 1 : 0);
    mod._remix_wasm_set_video_quality?.(videoQualityToInt(options?.videoQuality));
    mod._remix_wasm_set_video_effort?.(videoEffortToInt(options?.videoEffort));
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

  if (wantVqa && options?.videoW16Files && mod.FS) {
    try {
      mod.FS.mkdir('video');
    } catch {
      // already exists
    }
    for (const [relPath, data] of options.videoW16Files) {
      mod.FS.writeFile(relPath, data);
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
    audx_files: statsView.getUint32(64, true),
    audx_converted: statsView.getUint32(68, true),
    audx_errors: statsView.getUint32(72, true),
    vqa_files: statsView.getUint32(76, true),
    vqa_converted: statsView.getUint32(80, true),
    vqa_omitted: statsView.getUint32(84, true),
    vqa_already_stv: statsView.getUint32(88, true),
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
      omit: view.getInt32(84, true),
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

function clearVideoSidecars(mod: RemixModule): void {
  if (!mod.FS?.readdir) return;
  let names: string[];
  try {
    names = mod.FS.readdir('video');
  } catch {
    return;
  }
  for (const name of names) {
    if (name === '.' || name === '..') continue;
    try {
      mod.FS.unlink(`video/${name}`);
    } catch {
      // ignore
    }
  }
}

function installVideoSidecars(mod: RemixModule, files: [string, Uint8Array][]): void {
  try {
    mod.FS!.mkdir('video');
  } catch {
    // already exists
  }
  clearVideoSidecars(mod);
  for (const [relPath, data] of files) {
    mod.FS!.writeFile(relPath, data);
  }
}

export type EncodeVqaResult =
  | { status: 'ok'; stv: Uint8Array }
  | { status: 'omit' }
  | { status: 'error'; message: string };

/**
 * Encode a single VQA→STVQ using MEMFS /in.vqa → /out.stv.
 * Each call should pass only the video/*.w16 sidecars for this CRC.
 */
export async function encodeVqaBytes(
  vqa: Uint8Array,
  crc: number,
  baseUrl = '/',
  options?: Pick<RemixMixOptions, 'videoQuality' | 'videoEffort' | 'videoW16Files'>,
  onProgress?: RemixProgressHandler,
): Promise<EncodeVqaResult> {
  const mod = await loadRemixModule(baseUrl);
  attachProgressHandler(mod);
  activeProgressHandler = onProgress ?? null;

  if (typeof mod._remix_wasm_encode_vqa !== 'function') {
    activeProgressHandler = null;
    throw new Error(
      'VQA→STVQ encode_vqa is not available in this remix-web build — rebuild remix.wasm',
    );
  }

  mod._remix_wasm_set_vqa_enabled?.(1);
  mod._remix_wasm_set_video_quality?.(videoQualityToInt(options?.videoQuality));
  mod._remix_wasm_set_video_effort?.(videoEffortToInt(options?.videoEffort));

  if (options?.videoW16Files && mod.FS) {
    installVideoSidecars(mod, options.videoW16Files);
  }

  writeMemfsFile(mod, WASM_IN_VQA_PATH, vqa);

  try {
    const rc = mod._remix_wasm_encode_vqa(crc >>> 0);
    if (rc === 1) {
      const stv = readMemfsFile(mod, WASM_OUT_STV_PATH);
      try {
        mod.FS?.unlink(WASM_OUT_STV_PATH);
      } catch {
        // ignore
      }
      return { status: 'ok', stv };
    }
    if (rc < 0) {
      return { status: 'omit' };
    }
    return { status: 'error', message: `remix_wasm_encode_vqa failed (rc=${rc})` };
  } finally {
    activeProgressHandler = null;
    try {
      mod.FS?.unlink(WASM_IN_VQA_PATH);
    } catch {
      // ignore
    }
    try {
      mod.FS?.unlink(WASM_OUT_STV_PATH);
    } catch {
      // ignore
    }
  }
}

export async function remixMixBytes(
  input: Uint8Array,
  baseUrl = '/',
  options?: RemixMixOptions,
  onProgress?: RemixProgressHandler,
): Promise<{
  output: Uint8Array;
  stats: RemixStats;
  entries: RemixEntry[];
  shpxPool?: Uint8Array;
  audxPool?: Uint8Array;
}> {
  const mod = await loadRemixModule(baseUrl);
  attachProgressHandler(mod);
  activeProgressHandler = onProgress ?? null;
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
    const audxPool = options?.convertAudx
      ? readAudxPool(mod, resolveAudxPoolId(options))
      : undefined;
    return { output, stats, entries, shpxPool, audxPool };
  } finally {
    activeProgressHandler = null;
    mod._free(statsPtr);
    cleanupWorkfiles(mod);
  }
}

export async function remixMergeMixBytes(
  inputA: Uint8Array,
  inputB: Uint8Array,
  baseUrl = '/',
  options?: RemixMixOptions,
  onProgress?: RemixProgressHandler,
): Promise<{ output: Uint8Array; stats: RemixStats; entries: RemixEntry[]; shpxPool?: Uint8Array; audxPool?: Uint8Array }> {
  const mod = await loadRemixModule(baseUrl);
  attachProgressHandler(mod);
  activeProgressHandler = onProgress ?? null;
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
    const audxPool = options?.convertAudx
      ? readAudxPool(mod, resolveAudxPoolId(options))
      : undefined;
    return { output, stats, entries, shpxPool, audxPool };
  } finally {
    activeProgressHandler = null;
    mod._free(statsPtr);
    cleanupWorkfiles(mod);
  }
}
