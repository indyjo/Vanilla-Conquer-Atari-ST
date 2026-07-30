/**
 * MOVIES.MIX remix with windowed parallel VQA→STVQ encoding.
 *
 * 1. Merge (and/or remix) without VQA encode — keeps VQA payloads intact
 * 2. Encode VQAs with a worker pool (1–8, default 4) and staging window, ordered commit
 * 3. Assemble final MIX with STV substitutions / omits
 */
import {
  assembleMix,
  bodyOrderIndices,
  detectVqaKind,
  extractMixPayload,
  parseMix,
  type AssemblePayload,
} from './mix-format';
import {
  encodeVqaWindowed,
  vqaEncodeWindowForWorkers,
  VQA_ENCODE_WORKERS_DEFAULT,
} from './vqa-encode-pool';
import type {
  RemixEntry,
  RemixMixOptions,
  RemixProgressHandler,
  RemixStats,
} from './wasm-core';
import { remixMergeMixBytes, remixMixBytes } from './wasm-bridge';
import { clampVideoParallelism } from './target-version';

function emptyStats(): RemixStats {
  return {
    mix_files_ok: 1,
    mix_files_error: 0,
    mix_files_skipped: 0,
    payload_files: 0,
    audio_files: 0,
    audio_converted: 0,
    audio_already_ok: 0,
    payload_errors: 0,
    iconset_files: 0,
    iconset_converted: 0,
    iconset_already_st16: 0,
    iconset_errors: 0,
    shpx_files: 0,
    shpx_converted: 0,
    shpx_skipped: 0,
    shpx_errors: 0,
    audx_files: 0,
    audx_converted: 0,
    audx_errors: 0,
    vqa_files: 0,
    vqa_converted: 0,
    vqa_omitted: 0,
    vqa_already_stv: 0,
  };
}

function videoW16ForCrc(
  crc: number,
  all: [string, Uint8Array][] | undefined,
): [string, Uint8Array][] {
  if (!all) return [];
  const prefix = `video/${crc.toString(16).padStart(8, '0').toLowerCase()}.`;
  return all.filter(([path]) => path.toLowerCase().startsWith(prefix) && path.endsWith('.w16'));
}

export type MoviesRemixResult = {
  output: Uint8Array;
  stats: RemixStats;
  entries: RemixEntry[];
};

/**
 * Remix MOVIES.MIX with parallel VQA encodes.
 * Pass both discs for merge, or only `inputA` for a single source.
 */
export async function remixMoviesMix(
  inputA: Uint8Array,
  inputB: Uint8Array | null,
  baseUrl: string,
  options: RemixMixOptions,
  onProgress?: RemixProgressHandler,
  onLog?: (text: string) => void,
): Promise<MoviesRemixResult> {
  const workers = clampVideoParallelism(
    options.videoParallelism ?? VQA_ENCODE_WORKERS_DEFAULT,
  );
  const windowSize = vqaEncodeWindowForWorkers(workers);

  onLog?.(
    `Movies: parallel VQA encode (${workers} worker${workers === 1 ? '' : 's'}, window ${windowSize})…`,
  );

  /* Pass 1: merge/remix without VQA so payloads stay as VQA for the encode pool. */
  const pass1Opts: RemixMixOptions = {
    ...options,
    convertVqa: false,
    videoW16Files: undefined,
  };

  const pass1 = inputB
    ? await remixMergeMixBytes(inputA, inputB, baseUrl, pass1Opts)
    : await remixMixBytes(inputA, baseUrl, pass1Opts);

  const mixBytes = pass1.output;
  const parsed = parseMix(mixBytes);
  const order = bodyOrderIndices(parsed);

  type Work =
    | { kind: 'copy'; crc: number; payload: Uint8Array; typeIn: string }
    | { kind: 'stv'; crc: number; payload: Uint8Array }
    | { kind: 'vqa'; crc: number; payload: Uint8Array; vqaIndex: number };

  const work: Work[] = [];
  const vqaItems: { crc: number; vqa: Uint8Array }[] = [];

  for (const idx of order) {
    const ent = parsed.entries[idx];
    const payload = extractMixPayload(mixBytes, parsed, idx);
    if (payload.length === 0) {
      work.push({ kind: 'copy', crc: ent.crc, payload, typeIn: 'empty' });
      continue;
    }
    const kind = detectVqaKind(payload);
    if (kind === 'vqa') {
      work.push({ kind: 'vqa', crc: ent.crc, payload, vqaIndex: vqaItems.length });
      vqaItems.push({ crc: ent.crc, vqa: payload.slice() });
    } else if (kind === 'stv') {
      work.push({ kind: 'stv', crc: ent.crc, payload: payload.slice() });
    } else {
      work.push({ kind: 'copy', crc: ent.crc, payload: payload.slice(), typeIn: 'binary' });
    }
  }

  onLog?.(
    `Movies: ${vqaItems.length} VQA clip(s) to encode, ${work.length - vqaItems.length} other payload(s)`,
  );

  const encodeResults =
    vqaItems.length === 0
      ? []
      : await encodeVqaWindowed(vqaItems, {
          baseUrl,
          videoW16ForCrc: (crc) => videoW16ForCrc(crc, options.videoW16Files),
          videoQuality: options.videoQuality,
          videoEffort: options.videoEffort,
          workers,
          windowSize,
          onProgress,
        });

  const assembleItems: AssemblePayload[] = [];
  const entries: RemixEntry[] = [];
  const stats = emptyStats();
  stats.payload_files = work.length;

  for (const w of work) {
    if (w.kind === 'vqa') {
      stats.vqa_files++;
      const enc = encodeResults[w.vqaIndex];
      if (enc?.stv) {
        stats.vqa_converted++;
        assembleItems.push({ crc: w.crc, payload: enc.stv });
        entries.push({
          crc: w.crc,
          oldOffset: 0,
          oldSize: w.payload.length,
          newOffset: 0,
          newSize: enc.stv.length,
          typeIn: 'vqa',
          typeOut: 'stv',
          omit: 0,
        });
      } else {
        stats.vqa_omitted++;
        assembleItems.push({ crc: w.crc, payload: null });
        entries.push({
          crc: w.crc,
          oldOffset: 0,
          oldSize: w.payload.length,
          newOffset: 0,
          newSize: 0,
          typeIn: 'vqa',
          typeOut: 'omit',
          omit: 1,
        });
        onLog?.(
          `  ${w.crc.toString(16).padStart(8, '0').toUpperCase()} ${w.payload.length} vqa → omit`,
        );
      }
    } else if (w.kind === 'stv') {
      stats.vqa_files++;
      stats.vqa_already_stv++;
      assembleItems.push({ crc: w.crc, payload: w.payload });
      entries.push({
        crc: w.crc,
        oldOffset: 0,
        oldSize: w.payload.length,
        newOffset: 0,
        newSize: w.payload.length,
        typeIn: 'stv',
        typeOut: 'stv',
        omit: 0,
      });
    } else {
      assembleItems.push({ crc: w.crc, payload: w.payload });
      entries.push({
        crc: w.crc,
        oldOffset: 0,
        oldSize: w.payload.length,
        newOffset: 0,
        newSize: w.payload.length,
        typeIn: w.typeIn,
        typeOut: w.typeIn,
        omit: 0,
      });
    }
  }

  /* Carry over any audio stats from pass 1 (unlikely in MOVIES.MIX). */
  stats.audio_files = pass1.stats.audio_files;
  stats.audio_converted = pass1.stats.audio_converted;
  stats.audio_already_ok = pass1.stats.audio_already_ok;

  const output = assembleMix(assembleItems);
  return { output, stats, entries };
}
