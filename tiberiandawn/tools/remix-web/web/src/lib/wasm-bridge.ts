/**
 * Main-thread remix WASM bridge — proxies mix/merge work to a Web Worker
 * so long STVQ encodes do not freeze the UI.
 */
import type {
  RemixEncodeProgress,
  RemixEntry,
  RemixMixOptions,
  RemixProgressHandler,
  RemixStats,
} from './wasm-core';
import type { WorkerRequest, WorkerResponse } from './remix.worker';
import RemixWorker from './remix.worker.ts?worker';

export type {
  RemixEncodeProgress,
  RemixEntry,
  RemixMixOptions,
  RemixProgressHandler,
  RemixStats,
  RemixModule,
} from './wasm-core';

export type RemixResult = {
  output: Uint8Array;
  stats: RemixStats;
  entries: RemixEntry[];
  shpxPool?: Uint8Array;
  audxPool?: Uint8Array;
};

let worker: Worker | null = null;
let nextId = 1;
const pending = new Map<
  number,
  { resolve: (r: RemixResult) => void; reject: (e: Error) => void }
>();
let activeProgress: RemixProgressHandler | null = null;

function ensureWorker(): Worker {
  if (worker) return worker;
  worker = new RemixWorker();
  worker.onmessage = (ev: MessageEvent<WorkerResponse>) => {
    const msg = ev.data;
    if (msg.type === 'progress') {
      activeProgress?.({
        phase: msg.phase,
        crc: msg.crc,
        done: msg.done,
        total: msg.total,
      });
      return;
    }
    if (msg.type === 'encode_ok' || msg.type === 'encode_omit') {
      /* Handled by VqaEncodePool workers, not this bridge. */
      return;
    }
    const slot = pending.get(msg.id);
    if (!slot) return;
    pending.delete(msg.id);
    if (msg.type === 'error') {
      slot.reject(new Error(msg.message));
      return;
    }
    slot.resolve({
      output: new Uint8Array(msg.output),
      stats: msg.stats,
      entries: msg.entries,
      shpxPool: msg.shpxPool ? new Uint8Array(msg.shpxPool) : undefined,
      audxPool: msg.audxPool ? new Uint8Array(msg.audxPool) : undefined,
    });
  };
  worker.onerror = (ev) => {
    const err = new Error(ev.message || 'remix worker error');
    for (const [, slot] of pending) {
      slot.reject(err);
    }
    pending.clear();
    worker = null;
  };
  return worker;
}

function toArrayBuffer(u8: Uint8Array): ArrayBuffer {
  return u8.buffer.slice(u8.byteOffset, u8.byteOffset + u8.byteLength) as ArrayBuffer;
}

/** Clone options so transferable ArrayBuffers are not shared with the caller. */
function prepareOptions(options?: RemixMixOptions): {
  options?: RemixMixOptions;
  transfer: Transferable[];
} {
  if (!options) return { options: undefined, transfer: [] };
  const transfer: Transferable[] = [];
  const next: RemixMixOptions = { ...options };
  if (options.w16Bytes) {
    const buf = toArrayBuffer(options.w16Bytes);
    next.w16Bytes = new Uint8Array(buf);
    transfer.push(buf);
  }
  if (options.videoW16Files) {
    next.videoW16Files = options.videoW16Files.map(([path, data]) => {
      const buf = toArrayBuffer(data);
      transfer.push(buf);
      return [path, new Uint8Array(buf)];
    });
  }
  return { options: next, transfer };
}

function callWorker(msg: WorkerRequest, transfer: Transferable[]): Promise<RemixResult> {
  const w = ensureWorker();
  return new Promise((resolve, reject) => {
    pending.set(msg.id, { resolve, reject });
    w.postMessage(msg, transfer);
  });
}

/** Resolve BASE_URL against the page — workers often live under /assets/. */
function absoluteBaseUrl(baseUrl: string): string {
  return new URL(baseUrl, window.location.href).href;
}

export async function remixMixBytes(
  input: Uint8Array,
  baseUrl = '/',
  options?: RemixMixOptions,
  onProgress?: RemixProgressHandler,
): Promise<RemixResult> {
  const id = nextId++;
  const inputBuf = toArrayBuffer(input);
  const prepared = prepareOptions(options);
  activeProgress = onProgress ?? null;
  try {
    return await callWorker(
      {
        id,
        type: 'mix',
        baseUrl: absoluteBaseUrl(baseUrl),
        input: inputBuf,
        options: prepared.options,
      },
      [inputBuf, ...prepared.transfer],
    );
  } finally {
    activeProgress = null;
  }
}

export async function remixMergeMixBytes(
  inputA: Uint8Array,
  inputB: Uint8Array,
  baseUrl = '/',
  options?: RemixMixOptions,
  onProgress?: RemixProgressHandler,
): Promise<RemixResult> {
  const id = nextId++;
  const bufA = toArrayBuffer(inputA);
  const bufB = toArrayBuffer(inputB);
  const prepared = prepareOptions(options);
  activeProgress = onProgress ?? null;
  try {
    return await callWorker(
      {
        id,
        type: 'merge',
        baseUrl: absoluteBaseUrl(baseUrl),
        inputA: bufA,
        inputB: bufB,
        options: prepared.options,
      },
      [bufA, bufB, ...prepared.transfer],
    );
  } finally {
    activeProgress = null;
  }
}
