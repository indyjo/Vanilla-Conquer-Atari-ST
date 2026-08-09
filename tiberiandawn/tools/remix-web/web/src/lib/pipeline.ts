import { detectDiscLabel, extractFile, parseIso9660, type Iso9660Volume } from './iso9660';
import { flattenMixPath, listSelectedMixes, shouldMergeDualDisc } from './mix-policy';
import { assembleMix, mixFilenameCrc, type AssemblePayload } from './mix-format';
import { buildZip, lowercaseFileMap } from './zip';
import { extractReleaseAssets } from './release-zip';
import { remixMergeMixBytes, remixMixBytes, type RemixEntry, type RemixMixOptions } from './wasm-bridge';
import { remixMoviesMix } from './movies-remix';
import { entrySummary, notableEntryLines } from './entry-log';
import { audxPoolBasename, audxPoolIdForMix } from './audx';
import { isShpxEligibleMix, shpxPoolBasename, shpxPoolIdForMix } from './shpx';
import { isTheaterMix, requiredW16Stems, w16StemForTheaterMix } from './theater-st16';
import { harvestTransitAuds, injectAudsIntoMix } from './transit-aud';
import type { ContentOptions, DiscSelection, PipelineResult, ProcessProgress, ReleaseSelection, TargetVersion } from './types';
import { clampVideoParallelism } from './target-version';
import { vqaEncodeWindowForWorkers } from './vqa-encode-pool';

export interface PipelineRequest {
  gdi: DiscSelection;
  nod: DiscSelection;
  release: ReleaseSelection;
  contentOptions: ContentOptions;
  targetVersion: TargetVersion;
  wasmBaseUrl: string;
}

interface MixLocation {
  lba: number;
  size: number;
  isoPath: string;
  source: DiscSelection;
}

function logLine(
  progress: ProcessProgress,
  level: ProcessProgress['log'][number]['level'],
  text: string,
): ProcessProgress {
  return {
    ...progress,
    log: [...progress.log, { level, text }],
  };
}

function encodeProgressUpdate(
  progress: ProcessProgress,
  phase: string,
  crc: number,
  done: number,
  total: number,
): ProcessProgress {
  const label = crc.toString(16).padStart(8, '0').toUpperCase();
  const isStart = phase === 'start';
  const isDone = phase === 'done';
  let next = progress;

  /* One ProcessLog line when a VQA encode begins (same style as other conversions). */
  if (isStart) {
    const line =
      done > 0 ? `  ${label} ${done} vqa → stv` : `  ${label} vqa → stv`;
    next = logLine(next, 'info', line);
  }

  const prev = next.encodes ?? [];
  if (isDone) {
    const encodes = prev.filter((e) => e.label !== label);
    return {
      ...next,
      encodes: encodes.length > 0 ? encodes : undefined,
    };
  }

  const job = {
    phase: isStart ? 'start' : phase,
    label,
    done: isStart ? 0 : done,
    total: isStart ? 0 : total,
  };
  const idx = prev.findIndex((e) => e.label === label);
  const encodes =
    idx >= 0
      ? prev.map((e, i) => (i === idx ? job : e))
      : [...prev, job];

  return { ...next, encodes };
}

async function tick(): Promise<void> {
  await new Promise((resolve) => requestAnimationFrame(resolve));
}

/** Overall progress scale — MOVIES.MIX encoding dominates wall time. */
const PROGRESS_SCALE = 10000;
const MOVIES_PROGRESS_SHARE = 0.95;

function isMoviesMix(base: string): boolean {
  return base.toUpperCase() === 'MOVIES.MIX';
}

/** Work-unit budget: ~95% for movies VQA encodes when present; rest split across other mixes + zip. */
function progressBudgets(selected: string[], moviesEnabled: boolean): {
  total: number;
  moviesUnits: number;
  otherPerStep: number;
  otherSteps: number;
} {
  const hasMovies = moviesEnabled && selected.some(isMoviesMix);
  const otherSteps = selected.filter((b) => !isMoviesMix(b)).length + 1; /* + zip */
  const moviesUnits = hasMovies ? Math.round(PROGRESS_SCALE * MOVIES_PROGRESS_SHARE) : 0;
  const otherUnits = PROGRESS_SCALE - moviesUnits;
  const otherPerStep = otherSteps > 0 ? Math.floor(otherUnits / otherSteps) : 0;
  return { total: PROGRESS_SCALE, moviesUnits, otherPerStep, otherSteps };
}

/** Build ATARIST.MIX from root *.w16 release assets (CRC from uppercase basename). */
function buildAtariStMix(w16Files: Map<string, Uint8Array>): Uint8Array {
  const items: AssemblePayload[] = [];
  for (const [name, data] of w16Files) {
    items.push({ crc: mixFilenameCrc(name), payload: data });
  }
  return assembleMix(items);
}

function buildMixMap(volume: Iso9660Volume, disc: DiscSelection): Map<string, MixLocation> {
  const map = new Map<string, MixLocation>();
  for (const entry of volume.entries) {
    if (entry.isDir) continue;
    const base = flattenMixPath(entry.path);
    if (!map.has(base)) {
      map.set(base, {
        lba: entry.lba,
        size: entry.size,
        isoPath: entry.path,
        source: disc,
      });
    }
  }
  return map;
}

function collectMixPaths(...volumes: Iso9660Volume[]): string[] {
  const paths: string[] = [];
  for (const volume of volumes) {
    for (const entry of volume.entries) {
      if (!entry.isDir && entry.path.toUpperCase().endsWith('.MIX')) {
        paths.push(entry.path);
      }
    }
  }
  return paths;
}

function remixLogLine(
  base: string,
  entries: RemixEntry[],
  stats: {
    audio_converted: number;
    iconset_converted: number;
    iconset_already_st16: number;
    shpx_converted: number;
    vqa_converted?: number;
    vqa_omitted?: number;
  },
): string {
  let line = `Remixed ${base}: ${entrySummary(entries)} (${stats.audio_converted} audio converted`;
  if (stats.iconset_converted > 0 || stats.iconset_already_st16 > 0) {
    line += `, ${stats.iconset_converted} iconset ST16, ${stats.iconset_already_st16} already ST16`;
  }
  if (stats.shpx_converted > 0) {
    line += `, ${stats.shpx_converted} shape SHPX`;
  }
  if ((stats.vqa_converted ?? 0) > 0 || (stats.vqa_omitted ?? 0) > 0) {
    line += `, ${stats.vqa_converted ?? 0} VQA→STVQ, ${stats.vqa_omitted ?? 0} omitted`;
  }
  line += ')';
  return line;
}

function formatKb(bytes: number): string {
  const kb = bytes / 1024;
  return kb >= 100 ? String(Math.round(kb)) : kb.toFixed(1);
}

function savedLogLine(base: string, beforeBytes: number, afterBytes: number): string {
  const saved = beforeBytes - afterBytes;
  if (saved <= 0) {
    return `${base}: no size reduction (${formatKb(afterBytes)} KB out)`;
  }
  return `${base}: saved ${formatKb(saved)} KB (${formatKb(beforeBytes)} → ${formatKb(afterBytes)} KB)`;
}

function w16BytesForMix(
  mixBasename: string,
  releaseFiles: Map<string, Uint8Array>,
): Uint8Array | undefined {
  const stem = w16StemForTheaterMix(mixBasename);
  if (!stem) return undefined;
  const key = `${stem.toLowerCase()}.w16`;
  const data = releaseFiles.get(key);
  if (!data) {
    throw new Error(`Missing ${stem}.W16 in release ZIP (required for ST16 conversion of ${mixBasename})`);
  }
  return data;
}

function remixOptionsForMix(
  mixBasename: string,
  contentOptions: ContentOptions,
  releaseFiles: Map<string, Uint8Array>,
  targetVersion: TargetVersion,
): RemixMixOptions {
  const theater = isTheaterMix(mixBasename);
  const force03 = targetVersion === '0.3.x';
  const st16Enabled = (force03 || contentOptions.convertSt16Iconsets) && theater;
  const shpxPoolId = shpxPoolIdForMix(mixBasename);
  const shpxEnabled = (force03 || contentOptions.convertShpx) && shpxPoolId !== null;
  const audxPoolId = audxPoolIdForMix(mixBasename);
  const audxEnabled =
    force03 &&
    contentOptions.speechAndSfx &&
    audxPoolId !== null &&
    (mixBasename.toUpperCase() !== 'SCORES.MIX' || contentOptions.musicScores);
  const isMovies = mixBasename.toUpperCase() === 'MOVIES.MIX';
  const convertVqa = contentOptions.movieSequences && isMovies;
  let w16Bytes: Uint8Array | undefined;
  if (st16Enabled) {
    w16Bytes = w16BytesForMix(mixBasename, releaseFiles);
  }
  if (convertVqa) {
    let videoCount = 0;
    for (const key of releaseFiles.keys()) {
      if (key.startsWith('video/') && key.endsWith('.w16')) videoCount++;
    }
    if (videoCount === 0) {
      throw new Error('Release ZIP has no video/*.w16 sidecars (required for movie encoding)');
    }
  }
  return {
    convertSt16Iconsets: st16Enabled,
    convertShpx: shpxEnabled,
    convertAudx: audxEnabled,
    convertVqa,
    videoQuality: contentOptions.videoQuality,
    videoEffort: contentOptions.videoEffort,
    videoParallelism: contentOptions.videoParallelism,
    mixBasename,
    shpxPoolId: shpxPoolId ?? undefined,
    audxPoolId: audxPoolId ?? undefined,
    w16Bytes,
    videoW16Files: convertVqa && releaseFiles
      ? [...releaseFiles.entries()].filter(([k]) => k.startsWith('video/') && k.endsWith('.w16'))
      : undefined,
  };
}

function storeShpxPool(
  outputFiles: Map<string, Uint8Array>,
  pool: Uint8Array | undefined,
  poolId: number,
): void {
  if (!pool) return;
  outputFiles.set(shpxPoolBasename(poolId), pool);
}

function storeAudxPool(
  outputFiles: Map<string, Uint8Array>,
  pool: Uint8Array | undefined,
  poolId: number,
): void {
  if (!pool) return;
  outputFiles.set(audxPoolBasename(poolId), pool);
}

export async function runPipeline(
  req: PipelineRequest,
  onProgress: (progress: ProcessProgress) => void,
): Promise<PipelineResult> {
  let progress: ProcessProgress = {
    phase: 'scan',
    done: 0,
    total: 0,
    log: [{ level: 'info', text: 'Scanning ISO filesystems…' }],
  };
  onProgress(progress);
  await tick();

  const gdiVolume = await parseIso9660(req.gdi.file);
  const gdiLabel = detectDiscLabel(gdiVolume.volumeId);
  if (gdiLabel !== 'GDI') {
    throw new Error(
      `GDI disc has unexpected volume label "${gdiVolume.volumeId}" — expected GDI`,
    );
  }

  const nodVolume = await parseIso9660(req.nod.file);
  const nodLabel = detectDiscLabel(nodVolume.volumeId);
  if (nodLabel !== 'NOD') {
    throw new Error(
      `NOD disc has unexpected volume label "${nodVolume.volumeId}" — expected NOD`,
    );
  }

  progress = logLine(progress, 'info', `GDI disc: ${gdiVolume.volumeId}`);
  onProgress(progress);
  await tick();

  progress = logLine(progress, 'info', `NOD disc: ${nodVolume.volumeId}`);
  onProgress(progress);
  await tick();

  const gdiMap = buildMixMap(gdiVolume, req.gdi);
  const nodMap = buildMixMap(nodVolume, req.nod);

  const mixPaths = collectMixPaths(gdiVolume, nodVolume);
  const selected = listSelectedMixes(mixPaths, req.contentOptions);
  if (selected.length === 0) {
    throw new Error('No MIX files matched the selected content options');
  }

  const budgets = progressBudgets(selected, req.contentOptions.movieSequences);

  progress = {
    ...logLine(progress, 'info', `Will process ${selected.length} MIX file(s)`),
    phase: 'extract',
    total: budgets.total,
    done: 0,
  };
  onProgress(progress);
  await tick();

  const releaseAssets = await extractReleaseAssets(req.release.file);
  const releaseFiles = releaseAssets.files;

  if (req.contentOptions.convertSt16Iconsets) {
    const theaterMixes = selected.filter(isTheaterMix);
    if (theaterMixes.length > 0) {
      for (const stem of requiredW16Stems(theaterMixes)) {
        const key = `${stem.toLowerCase()}.w16`;
        if (!releaseFiles.has(key)) {
          throw new Error(
            `Release ZIP is missing ${stem}.W16 (required for ST16 conversion)`,
          );
        }
      }
      progress = logLine(
        progress,
        'info',
        `ST16: will convert iconsets in ${theaterMixes.length} theater MIX file(s)`,
      );
      onProgress(progress);
      await tick();
    }
  }

  if (req.contentOptions.movieSequences) {
    let videoCount = 0;
    for (const key of releaseFiles.keys()) {
      if (key.startsWith('video/') && key.endsWith('.w16')) videoCount++;
    }
    if (videoCount === 0) {
      throw new Error('Release ZIP has no video/*.w16 sidecars (required for movie encoding)');
    }
    const workers = clampVideoParallelism(req.contentOptions.videoParallelism);
    const windowSize = vqaEncodeWindowForWorkers(workers);
    progress = logLine(
      progress,
      'info',
      `Movies: will encode VQA→STVQ using ${videoCount} video/*.w16 sidecar(s) ` +
        `(${workers} worker${workers === 1 ? '' : 's'}, window ${windowSize})`,
    );
    onProgress(progress);
    await tick();
  }

  if (req.contentOptions.convertShpx) {
    const shpxMixes = selected.filter(isShpxEligibleMix);
    if (shpxMixes.length > 0) {
      progress = logLine(
        progress,
        'info',
        `SHPX: will convert KeyFrame shapes in ${shpxMixes.join(', ')}`,
      );
      onProgress(progress);
      await tick();
    }
  }

  /*
   * 0.3.x + Audio: move TRANSIT.MIX AUDs into SOUNDS.MIX before AUDX rempack so
   * metas land in the cached MIX (TRANSIT itself cannot be cached).
   * Only strip TRANSIT after SOUNDS has accepted the inject (SOUNDS sorts first).
   */
  let transitAudInject: AssemblePayload[] = [];
  let transitRawOriginal: Uint8Array | null = null;
  let strippedTransitMix: Uint8Array | null = null;
  let transitAudsMoved = false;
  const wantTransitAudMove =
    req.targetVersion === '0.3.x' &&
    req.contentOptions.speechAndSfx &&
    selected.includes('SOUNDS.MIX') &&
    selected.includes('TRANSIT.MIX');

  if (wantTransitAudMove) {
    const transitLoc = gdiMap.get('TRANSIT.MIX') ?? nodMap.get('TRANSIT.MIX');
    if (transitLoc) {
      progress = logLine(progress, 'info', 'Extracting TRANSIT.MIX for AUD→SOUNDS harvest…');
      onProgress(progress);
      await tick();
      transitRawOriginal = await extractFile(
        transitLoc.source.file,
        transitLoc.lba,
        transitLoc.size,
      );
      const harvested = harvestTransitAuds(transitRawOriginal);
      if (harvested.auds.length > 0) {
        transitAudInject = harvested.auds;
        strippedTransitMix = harvested.stripped;
        progress = logLine(
          progress,
          'info',
          `TRANSIT→SOUNDS: will move ${harvested.auds.length} AUD file(s) ` +
            `(${formatKb(harvested.audBytes)} KB) into SOUNDS for AUDX`,
        );
        onProgress(progress);
        await tick();
      }
    }
  }

  const outputFiles = new Map<string, Uint8Array>();

  for (const base of selected) {
    progress = { ...progress, phase: 'extract', current: base };
    onProgress(progress);
    await tick();

    if (shouldMergeDualDisc(base) && gdiMap.has(base) && nodMap.has(base)) {
      const gdiLoc = gdiMap.get(base)!;
      const nodLoc = nodMap.get(base)!;
      const remixOptsPreview = remixOptionsForMix(base, req.contentOptions, releaseFiles, req.targetVersion);
      progress = logLine(
        progress,
        'info',
        remixOptsPreview.convertVqa
          ? `Merging ${base} from GDI + NOD (then parallel VQA encode)`
          : `Merging ${base} from GDI + NOD (then REMIX once)`,
      );
      onProgress(progress);
      await tick();

      const rawGdi = await extractFile(gdiLoc.source.file, gdiLoc.lba, gdiLoc.size);
      const rawNod = await extractFile(nodLoc.source.file, nodLoc.lba, nodLoc.size);
      progress = logLine(
        progress,
        'info',
        `Extracted ${base} (${rawGdi.length} + ${rawNod.length} bytes)`,
      );
      onProgress(progress);
      await tick();

      progress = { ...progress, phase: 'remix', current: base };
      const remixOpts = remixOptsPreview;
      const inputBytes = Math.max(rawGdi.length, rawNod.length);

      let output: Uint8Array;
      let stats: Awaited<ReturnType<typeof remixMergeMixBytes>>['stats'];
      let entries: RemixEntry[];
      let shpxPool: Uint8Array | undefined;
      let audxPool: Uint8Array | undefined;

      if (remixOpts.convertVqa) {
        const moviesStart = progress.done;
        const movies = await remixMoviesMix(
          rawGdi,
          rawNod,
          req.wasmBaseUrl,
          remixOpts,
          (p) => {
            progress = encodeProgressUpdate(progress, p.phase, p.crc, p.done, p.total);
            onProgress(progress);
          },
          (text) => {
            progress = logLine(progress, 'info', text);
            onProgress(progress);
          },
          (_crc, completedBytes, totalBytes) => {
            const frac = totalBytes > 0 ? completedBytes / totalBytes : 1;
            progress = {
              ...progress,
              done: Math.min(
                budgets.total,
                Math.round(moviesStart + frac * budgets.moviesUnits),
              ),
            };
            onProgress(progress);
          },
        );
        output = movies.output;
        stats = movies.stats;
        entries = movies.entries;
        progress = {
          ...progress,
          done: Math.min(budgets.total, moviesStart + budgets.moviesUnits),
        };
      } else {
        progress = logLine(progress, 'info', 'Merge + REMIX (WebAssembly)…');
        onProgress(progress);
        await tick();
        const remixed = await remixMergeMixBytes(
          rawGdi,
          rawNod,
          req.wasmBaseUrl,
          remixOpts,
          (p) => {
            progress = encodeProgressUpdate(progress, p.phase, p.crc, p.done, p.total);
            onProgress(progress);
          },
        );
        output = remixed.output;
        stats = remixed.stats;
        entries = remixed.entries;
        shpxPool = remixed.shpxPool;
        audxPool = remixed.audxPool;
      }
      progress = { ...progress, encodes: undefined };
      progress = logLine(progress, 'info', remixLogLine(base, entries, stats));
      for (const line of notableEntryLines(entries)) {
        progress = logLine(progress, 'info', line);
      }
      progress = logLine(progress, 'info', savedLogLine(base, inputBytes, output.length));
      outputFiles.set(base, output);
      const poolId = remixOpts.shpxPoolId ?? 1;
      storeShpxPool(outputFiles, shpxPool, poolId);
      if (shpxPool) {
        progress = logLine(
          progress,
          'info',
          `Wrote ${shpxPoolBasename(poolId)} (${shpxPool.length} bytes)`,
        );
      }
      const audxId = remixOpts.audxPoolId ?? 5;
      storeAudxPool(outputFiles, audxPool, audxId);
      if (audxPool) {
        progress = logLine(
          progress,
          'info',
          `Wrote ${audxPoolBasename(audxId)} (${audxPool.length} bytes)`,
        );
      }
    } else {
      const loc = gdiMap.get(base) ?? nodMap.get(base);
      if (!loc) {
        progress = logLine(progress, 'warn', `Missing on both discs: ${base}`);
        if (!isMoviesMix(base)) {
          progress = {
            ...progress,
            done: Math.min(budgets.total, progress.done + budgets.otherPerStep),
          };
        }
        onProgress(progress);
        await tick();
        continue;
      }

      let raw: Uint8Array;
      if (base === 'TRANSIT.MIX' && transitRawOriginal) {
        if (transitAudsMoved && strippedTransitMix) {
          raw = strippedTransitMix;
          progress = logLine(
            progress,
            'info',
            `Using stripped ${base} (${raw.length} bytes; AUDs moved to SOUNDS.MIX)`,
          );
        } else {
          raw = transitRawOriginal;
          progress = logLine(
            progress,
            'info',
            `Using ${base} (${raw.length} bytes` +
              (transitAudInject.length > 0
                ? '; AUD move skipped — keeping AUDs in TRANSIT'
                : '') +
              ')',
          );
        }
      } else {
        raw = await extractFile(loc.source.file, loc.lba, loc.size);
        progress = logLine(
          progress,
          'info',
          `Extracted ${base} from ${loc.source.label} (${raw.length} bytes)`,
        );
      }
      onProgress(progress);
      await tick();

      if (base === 'SOUNDS.MIX' && transitAudInject.length > 0) {
        const injected = injectAudsIntoMix(raw, transitAudInject);
        raw = injected.mix;
        transitAudsMoved = injected.added > 0;
        progress = logLine(
          progress,
          'info',
          `Injected ${injected.added} TRANSIT AUD(s) into ${base}` +
            (injected.skipped > 0 ? ` (${injected.skipped} CRC already present)` : ''),
        );
        onProgress(progress);
        await tick();
      }

      progress = { ...progress, phase: 'remix', current: base };
      const remixOpts = remixOptionsForMix(base, req.contentOptions, releaseFiles, req.targetVersion);

      let output: Uint8Array;
      let stats: Awaited<ReturnType<typeof remixMixBytes>>['stats'];
      let entries: RemixEntry[];
      let shpxPool: Uint8Array | undefined;
      let audxPool: Uint8Array | undefined;

      if (remixOpts.convertVqa) {
        progress = logLine(progress, 'info', 'Parallel VQA encode…');
        onProgress(progress);
        await tick();
        const moviesStart = progress.done;
        const movies = await remixMoviesMix(
          raw,
          null,
          req.wasmBaseUrl,
          remixOpts,
          (p) => {
            progress = encodeProgressUpdate(progress, p.phase, p.crc, p.done, p.total);
            onProgress(progress);
          },
          (text) => {
            progress = logLine(progress, 'info', text);
            onProgress(progress);
          },
          (_crc, completedBytes, totalBytes) => {
            const frac = totalBytes > 0 ? completedBytes / totalBytes : 1;
            progress = {
              ...progress,
              done: Math.min(
                budgets.total,
                Math.round(moviesStart + frac * budgets.moviesUnits),
              ),
            };
            onProgress(progress);
          },
        );
        output = movies.output;
        stats = movies.stats;
        entries = movies.entries;
        progress = {
          ...progress,
          done: Math.min(budgets.total, moviesStart + budgets.moviesUnits),
        };
      } else {
        progress = logLine(progress, 'info', 'REMIX (WebAssembly)…');
        onProgress(progress);
        await tick();
        const remixed = await remixMixBytes(
          raw,
          req.wasmBaseUrl,
          remixOpts,
          (p) => {
            progress = encodeProgressUpdate(progress, p.phase, p.crc, p.done, p.total);
            onProgress(progress);
          },
        );
        output = remixed.output;
        stats = remixed.stats;
        entries = remixed.entries;
        shpxPool = remixed.shpxPool;
        audxPool = remixed.audxPool;
      }
      progress = { ...progress, encodes: undefined };
      progress = logLine(progress, 'info', remixLogLine(base, entries, stats));
      for (const line of notableEntryLines(entries)) {
        progress = logLine(progress, 'info', line);
      }
      progress = logLine(progress, 'info', savedLogLine(base, raw.length, output.length));
      outputFiles.set(base, output);
      const poolId = remixOpts.shpxPoolId ?? 1;
      storeShpxPool(outputFiles, shpxPool, poolId);
      if (shpxPool) {
        progress = logLine(
          progress,
          'info',
          `Wrote ${shpxPoolBasename(poolId)} (${shpxPool.length} bytes)`,
        );
      }
      const audxId = remixOpts.audxPoolId ?? 5;
      storeAudxPool(outputFiles, audxPool, audxId);
      if (audxPool) {
        progress = logLine(
          progress,
          'info',
          `Wrote ${audxPoolBasename(audxId)} (${audxPool.length} bytes)`,
        );
      }
    }

    /* Movies VQA path already advanced via size-weighted callbacks. */
    const moviesWeighted =
      isMoviesMix(base) &&
      remixOptionsForMix(base, req.contentOptions, releaseFiles, req.targetVersion).convertVqa;
    if (!moviesWeighted) {
      progress = {
        ...progress,
        done: Math.min(budgets.total, progress.done + budgets.otherPerStep),
      };
    }
    onProgress(progress);
    await tick();
  }

  progress = { ...progress, phase: 'zip', current: undefined };
  onProgress(progress);
  await tick();

  progress = logLine(
    progress,
    'info',
    `Adding Atari ST release files from ${req.release.file.name}…`,
  );
  onProgress(progress);
  await tick();

  let skippedVideo = 0;
  const w16ForLocal = new Map<string, Uint8Array>();
  for (const [name, data] of releaseFiles) {
    if (name.startsWith('video/')) {
      skippedVideo++;
      continue;
    }
    if (name.endsWith('.w16')) {
      w16ForLocal.set(name, data);
      continue;
    }
    if (name === 'readme.txt' || name === 'readme.md') {
      continue;
    }
    if (outputFiles.has(name)) {
      progress = logLine(progress, 'warn', `Skipped release file (already in output): ${name}`);
    } else {
      outputFiles.set(name, data);
      progress = logLine(progress, 'info', `Included ${name} (${data.length} bytes)`);
    }
    onProgress(progress);
    await tick();
  }
  if (skippedVideo > 0) {
    progress = logLine(
      progress,
      'info',
      `Skipped ${skippedVideo} encode-only video/*.w16 sidecar(s)`,
    );
    onProgress(progress);
    await tick();
  }

  if (w16ForLocal.size > 0) {
    const mix = buildAtariStMix(w16ForLocal);
    outputFiles.set('ATARIST.MIX', mix);
    progress = logLine(
      progress,
      'info',
      `Wrote atarist.mix with ${w16ForLocal.size} .w16 file(s) (${mix.length} bytes)`,
    );
    onProgress(progress);
    await tick();
  }

  progress = {
    ...progress,
    done: Math.min(budgets.total, progress.done + budgets.otherPerStep),
  };
  const output = lowercaseFileMap(outputFiles);
  const zipBlob = buildZip(output);
  progress = logLine(
    progress,
    'info',
    `Created ZIP (${output.size} files, ${zipBlob.size} bytes)`,
  );
  progress = { ...progress, phase: 'done', done: budgets.total };
  onProgress(progress);

  return {
    zipBlob,
    fileNames: [...output.keys()],
    files: output,
  };
}
