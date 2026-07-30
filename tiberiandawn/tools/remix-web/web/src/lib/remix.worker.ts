/**
 * Web Worker that runs remix.wasm off the UI thread.
 * Loaded via Vite `?worker` (ES module in dev and build).
 *
 * Handles full MIX remix/merge and single-VQA encode jobs (for the parallel pool).
 */
import {
  encodeVqaBytes,
  remixMergeMixBytes,
  remixMixBytes,
  type RemixMixOptions,
  type RemixEntry,
  type RemixStats,
  type RemixEncodeProgress,
  type EncodeVqaResult,
} from './wasm-core';

export type RemixResult = {
  output: Uint8Array;
  stats: RemixStats;
  entries: RemixEntry[];
  shpxPool?: Uint8Array;
  audxPool?: Uint8Array;
};

export type WorkerRequest =
  | { id: number; type: 'mix'; baseUrl: string; input: ArrayBuffer; options?: RemixMixOptions }
  | {
      id: number;
      type: 'merge';
      baseUrl: string;
      inputA: ArrayBuffer;
      inputB: ArrayBuffer;
      options?: RemixMixOptions;
    }
  | {
      id: number;
      type: 'encode_vqa';
      baseUrl: string;
      crc: number;
      vqa: ArrayBuffer;
      videoQuality?: RemixMixOptions['videoQuality'];
      videoEffort?: RemixMixOptions['videoEffort'];
      videoW16Files: [string, ArrayBuffer][];
    };

export type WorkerResponse =
  | {
      id: number;
      type: 'ok';
      output: ArrayBuffer;
      stats: RemixStats;
      entries: RemixEntry[];
      shpxPool?: ArrayBuffer;
      audxPool?: ArrayBuffer;
    }
  | {
      id: number;
      type: 'encode_ok';
      crc: number;
      stv: ArrayBuffer;
    }
  | {
      id: number;
      type: 'encode_omit';
      crc: number;
    }
  | { id: number; type: 'error'; message: string }
  | ({ type: 'progress'; jobId: number } & RemixEncodeProgress);

function toArrayBuffer(u8: Uint8Array): ArrayBuffer {
  return u8.buffer.slice(u8.byteOffset, u8.byteOffset + u8.byteLength) as ArrayBuffer;
}

function emitProgress(jobId: number, p: RemixEncodeProgress): void {
  const msg: WorkerResponse = { type: 'progress', jobId, ...p };
  self.postMessage(msg);
}

self.onmessage = async (ev: MessageEvent<WorkerRequest>) => {
  const msg = ev.data;
  try {
    if (msg.type === 'encode_vqa') {
      const videoW16Files: [string, Uint8Array][] = msg.videoW16Files.map(([path, buf]) => [
        path,
        new Uint8Array(buf),
      ]);
      const result: EncodeVqaResult = await encodeVqaBytes(
        new Uint8Array(msg.vqa),
        msg.crc,
        msg.baseUrl,
        {
          videoQuality: msg.videoQuality,
          videoEffort: msg.videoEffort,
          videoW16Files,
        },
        (p) => emitProgress(msg.id, p),
      );
      if (result.status === 'ok') {
        const stv = toArrayBuffer(result.stv);
        const response: WorkerResponse = {
          id: msg.id,
          type: 'encode_ok',
          crc: msg.crc,
          stv,
        };
        self.postMessage(response, { transfer: [stv] });
      } else if (result.status === 'omit') {
        const response: WorkerResponse = { id: msg.id, type: 'encode_omit', crc: msg.crc };
        self.postMessage(response);
      } else {
        throw new Error(result.message);
      }
      return;
    }

    let result: RemixResult;
    if (msg.type === 'mix') {
      result = await remixMixBytes(new Uint8Array(msg.input), msg.baseUrl, msg.options, (p) =>
        emitProgress(msg.id, p),
      );
    } else if (msg.type === 'merge') {
      result = await remixMergeMixBytes(
        new Uint8Array(msg.inputA),
        new Uint8Array(msg.inputB),
        msg.baseUrl,
        msg.options,
        (p) => emitProgress(msg.id, p),
      );
    } else {
      throw new Error(`unknown worker message type`);
    }

    const output = toArrayBuffer(result.output);
    const shpxPool = result.shpxPool ? toArrayBuffer(result.shpxPool) : undefined;
    const audxPool = result.audxPool ? toArrayBuffer(result.audxPool) : undefined;
    const response: WorkerResponse = {
      id: msg.id,
      type: 'ok',
      output,
      stats: result.stats,
      entries: result.entries,
      shpxPool,
      audxPool,
    };
    const transfer: Transferable[] = [output];
    if (shpxPool) transfer.push(shpxPool);
    if (audxPool) transfer.push(audxPool);
    self.postMessage(response, { transfer });
  } catch (err) {
    const response: WorkerResponse = {
      id: msg.id,
      type: 'error',
      message: err instanceof Error ? err.message : String(err),
    };
    self.postMessage(response);
  }
};
