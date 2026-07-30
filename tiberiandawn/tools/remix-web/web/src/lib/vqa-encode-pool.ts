/**
 * Windowed parallel VQA→STVQ encode pool for remix-web.
 *
 * - Input window: at most `windowSize` unfinished VQAs staged (completed results
 *   waiting for ordered commit do not consume window slots)
 * - Up to `workers` concurrent encode workers (each with its own remix.wasm)
 * - Results are committed in source MIX body order
 */
import type { RemixEncodeProgress, RemixProgressHandler } from './wasm-core';
import type { WorkerRequest, WorkerResponse } from './remix.worker';
import RemixWorker from './remix.worker.ts?worker';

export const VQA_ENCODE_WORKERS_DEFAULT = 6;

/** Staging slack beyond worker count so a finished job can be replaced immediately. */
export function vqaEncodeWindowForWorkers(workers: number): number {
  const w = Math.max(1, Math.min(16, Math.round(workers)));
  return w + 2;
}

export type VqaEncodeJob = {
  /** Stable id for progress multiplexing. */
  id: number;
  crc: number;
  vqa: Uint8Array;
  videoW16Files: [string, Uint8Array][];
  videoQuality?: 'low' | 'medium' | 'high';
  videoEffort?: 'fast' | 'normal' | 'thorough';
};

export type VqaEncodeOutcome =
  | { status: 'ok'; crc: number; stv: Uint8Array }
  | { status: 'omit'; crc: number };

type PendingEncode = {
  resolve: (r: VqaEncodeOutcome) => void;
  reject: (e: Error) => void;
  onProgress?: RemixProgressHandler;
};

type PoolWorker = {
  worker: Worker;
  busyJobId: number | null;
};

function toArrayBuffer(u8: Uint8Array): ArrayBuffer {
  return u8.buffer.slice(u8.byteOffset, u8.byteOffset + u8.byteLength) as ArrayBuffer;
}

function absoluteBaseUrl(baseUrl: string): string {
  return new URL(baseUrl, window.location.href).href;
}

/**
 * Fixed-size pool of encode workers. Call terminate() when movies remix finishes.
 */
export class VqaEncodePool {
  private readonly workers: PoolWorker[] = [];
  private readonly pending = new Map<number, PendingEncode>();
  private nextId = 1;
  private readonly baseUrl: string;
  private readonly queue: {
    job: VqaEncodeJob;
    resolve: (r: VqaEncodeOutcome) => void;
    reject: (e: Error) => void;
    onProgress?: RemixProgressHandler;
  }[] = [];

  constructor(baseUrl: string, workerCount = VQA_ENCODE_WORKERS_DEFAULT) {
    this.baseUrl = absoluteBaseUrl(baseUrl);
    const n = Math.max(1, Math.min(16, workerCount));
    for (let i = 0; i < n; i++) {
      this.workers.push({ worker: this.spawnWorker(), busyJobId: null });
    }
  }

  private spawnWorker(): Worker {
    const worker = new RemixWorker();
    worker.onmessage = (ev: MessageEvent<WorkerResponse>) => {
      this.onWorkerMessage(ev.data);
    };
    worker.onerror = (ev) => {
      const err = new Error(ev.message || 'VQA encode worker error');
      for (const [, slot] of this.pending) {
        slot.reject(err);
      }
      this.pending.clear();
      for (const w of this.workers) {
        w.busyJobId = null;
      }
    };
    return worker;
  }

  private onWorkerMessage(msg: WorkerResponse): void {
    if (msg.type === 'progress') {
      const slot = this.pending.get(msg.jobId);
      slot?.onProgress?.({
        phase: msg.phase,
        crc: msg.crc,
        done: msg.done,
        total: msg.total,
      });
      return;
    }

    if (msg.type !== 'encode_ok' && msg.type !== 'encode_omit' && msg.type !== 'error') {
      return;
    }

    const slot = this.pending.get(msg.id);
    if (!slot) return;
    this.pending.delete(msg.id);

    for (const w of this.workers) {
      if (w.busyJobId === msg.id) {
        w.busyJobId = null;
        break;
      }
    }

    if (msg.type === 'error') {
      slot.reject(new Error(msg.message));
    } else if (msg.type === 'encode_omit') {
      slot.resolve({ status: 'omit', crc: msg.crc });
    } else {
      slot.resolve({ status: 'ok', crc: msg.crc, stv: new Uint8Array(msg.stv) });
    }

    this.pumpQueue();
  }

  private pumpQueue(): void {
    while (this.queue.length > 0) {
      const free = this.workers.find((w) => w.busyJobId === null);
      if (!free) break;
      const item = this.queue.shift()!;
      const id = item.job.id;
      free.busyJobId = id;
      this.pending.set(id, {
        resolve: item.resolve,
        reject: item.reject,
        onProgress: item.onProgress,
      });

      const vqaBuf = toArrayBuffer(item.job.vqa);
      const transfer: Transferable[] = [vqaBuf];
      const videoW16Files: [string, ArrayBuffer][] = item.job.videoW16Files.map(([path, data]) => {
        const buf = toArrayBuffer(data);
        transfer.push(buf);
        return [path, buf];
      });

      const req: WorkerRequest = {
        id,
        type: 'encode_vqa',
        baseUrl: this.baseUrl,
        crc: item.job.crc,
        vqa: vqaBuf,
        videoQuality: item.job.videoQuality,
        videoEffort: item.job.videoEffort,
        videoW16Files,
      };
      free.worker.postMessage(req, transfer);
    }
  }

  encode(job: Omit<VqaEncodeJob, 'id'>, onProgress?: RemixProgressHandler): Promise<VqaEncodeOutcome> {
    const id = this.nextId++;
    const full: VqaEncodeJob = { ...job, id };
    return new Promise((resolve, reject) => {
      this.queue.push({ job: full, resolve, reject, onProgress });
      this.pumpQueue();
    });
  }

  terminate(): void {
    for (const w of this.workers) {
      w.worker.terminate();
      w.busyJobId = null;
    }
    this.workers.length = 0;
    for (const [, slot] of this.pending) {
      slot.reject(new Error('VQA encode pool terminated'));
    }
    this.pending.clear();
    this.queue.length = 0;
  }
}

export type WindowedEncodeItem = {
  crc: number;
  /** Extracted VQA bytes; released after encode is dispatched or omitted. */
  vqa: Uint8Array;
};

export type WindowedEncodeResult = {
  crc: number;
  /** null = omit from MIX. */
  stv: Uint8Array | null;
};

/**
 * Encode VQAs with a sliding window and ordered completion.
 * `items` must be in MIX body order (only the VQA entries).
 */
export async function encodeVqaWindowed(
  items: WindowedEncodeItem[],
  opts: {
    baseUrl: string;
    videoW16ForCrc: (crc: number) => [string, Uint8Array][];
    videoQuality?: 'low' | 'medium' | 'high';
    videoEffort?: 'fast' | 'normal' | 'thorough';
    windowSize?: number;
    workers?: number;
    onProgress?: RemixProgressHandler;
    onJobStart?: (crc: number, size: number) => void;
    /** Called when a VQA encode finishes (ok or omit), with input VQA byte size. */
    onJobComplete?: (crc: number, size: number) => void;
  },
): Promise<WindowedEncodeResult[]> {
  const workers = Math.max(1, Math.min(16, opts.workers ?? VQA_ENCODE_WORKERS_DEFAULT));
  const windowSize = opts.windowSize ?? vqaEncodeWindowForWorkers(workers);
  const pool = new VqaEncodePool(opts.baseUrl, workers);
  const results: (WindowedEncodeResult | undefined)[] = new Array(items.length);
  let nextToStage = 0;
  let nextToCommit = 0;
  const inflight = new Map<number, Promise<void>>();

  /** Unfinished jobs in the staging window (completed-but-uncommitted do not count). */
  const activeInWindow = (): number => {
    let n = 0;
    for (let i = nextToCommit; i < nextToStage; i++) {
      if (results[i] === undefined) n++;
    }
    return n;
  };

  const tryStage = (): void => {
    while (activeInWindow() < windowSize && nextToStage < items.length) {
      const index = nextToStage++;
      const item = items[index];

      const sidecars = opts.videoW16ForCrc(item.crc);
      if (sidecars.length === 0) {
        results[index] = { crc: item.crc, stv: null };
        opts.onJobComplete?.(item.crc, item.vqa.length);
        continue;
      }

      opts.onJobStart?.(item.crc, item.vqa.length);

      const p = pool
        .encode(
          {
            crc: item.crc,
            vqa: item.vqa,
            videoW16Files: sidecars,
            videoQuality: opts.videoQuality,
            videoEffort: opts.videoEffort,
          },
          opts.onProgress,
        )
        .then((outcome) => {
          results[index] =
            outcome.status === 'ok'
              ? { crc: outcome.crc, stv: outcome.stv }
              : { crc: outcome.crc, stv: null };
          opts.onJobComplete?.(item.crc, item.vqa.length);
          opts.onProgress?.({ phase: 'done', crc: item.crc, done: 0, total: 0 });
          inflight.delete(index);
          /* Free window slot → stage more so idle workers stay busy. */
          tryStage();
        })
        .catch((err) => {
          opts.onJobComplete?.(item.crc, item.vqa.length);
          opts.onProgress?.({ phase: 'done', crc: item.crc, done: 0, total: 0 });
          inflight.delete(index);
          tryStage();
          throw err;
        });
      inflight.set(index, p);
    }
  };

  try {
    tryStage();
    while (nextToCommit < items.length) {
      while (nextToCommit < items.length && results[nextToCommit] !== undefined) {
        nextToCommit++;
        tryStage();
      }
      if (nextToCommit >= items.length) break;

      const waitFor = inflight.get(nextToCommit);
      if (waitFor) {
        await waitFor;
      } else if (inflight.size > 0) {
        await Promise.race(inflight.values());
      } else {
        tryStage();
        if (inflight.size === 0 && results[nextToCommit] === undefined) {
          throw new Error('VQA encode window stalled');
        }
      }
    }
  } finally {
    pool.terminate();
  }

  return results.map((r, i) => {
    if (!r) {
      throw new Error(`Missing encode result for VQA index ${i}`);
    }
    return r;
  });
}

/** Re-export progress type for callers. */
export type { RemixEncodeProgress };
