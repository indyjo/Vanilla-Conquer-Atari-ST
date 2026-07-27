import { detectDiscLabel, extractFile, parseIso9660, type Iso9660Volume } from './iso9660';
import { flattenMixPath, listSelectedMixes, shouldMergeDualDisc } from './mix-policy';
import { buildZip, lowercaseFileMap } from './zip';
import { extractReleaseAssets } from './release-zip';
import { remixMergeMixBytes, remixMixBytes, type RemixEntry, type RemixMixOptions } from './wasm-bridge';
import { remixMoviesMix } from './movies-remix';
import { entrySummary, notableEntryLines } from './entry-log';
import { isShpxEligibleMix, shpxPoolBasename, shpxPoolIdForMix } from './shpx';
import { isTheaterMix, requiredW16Stems, w16StemForTheaterMix } from './theater-st16';
import type { ContentOptions, DiscSelection, PipelineResult, ProcessProgress, ReleaseSelection, TargetVersion } from './types';
import { clampVideoParallelism } from './target-version';
import { vqaEncodeWindowForWorkers } from './vqa-encode-pool';

export interface PipelineRequest {
  gdi: DiscSelection;
  nod: DiscSelection;
  release?: ReleaseSelection | null;
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
  releaseFiles: Map<string, Uint8Array> | null,
): RemixMixOptions {
  const theater = isTheaterMix(mixBasename);
  const st16Enabled = contentOptions.convertSt16Iconsets && theater;
  const shpxPoolId = shpxPoolIdForMix(mixBasename);
  const shpxEnabled = contentOptions.convertShpx && shpxPoolId !== null;
  const isMovies = mixBasename.toUpperCase() === 'MOVIES.MIX';
  const convertVqa = contentOptions.movieSequences && isMovies;
  let w16Bytes: Uint8Array | undefined;
  if (st16Enabled) {
    if (!releaseFiles) {
      throw new Error(
        'ST16 iconset conversion requires the itch.io release ZIP with matching *.W16 files',
      );
    }
    w16Bytes = w16BytesForMix(mixBasename, releaseFiles);
  }
  if (convertVqa) {
    if (!releaseFiles) {
      throw new Error(
        'Movie encoding requires the itch.io release ZIP with video/*.w16 sidecars',
      );
    }
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
    convertVqa,
    videoQuality: contentOptions.videoQuality,
    videoEffort: contentOptions.videoEffort,
    videoParallelism: contentOptions.videoParallelism,
    mixBasename,
    shpxPoolId: shpxPoolId ?? undefined,
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

  progress = {
    ...logLine(progress, 'info', `Will process ${selected.length} MIX file(s)`),
    phase: 'extract',
    total: selected.length,
    done: 0,
  };
  onProgress(progress);
  await tick();

  let releaseFiles: Map<string, Uint8Array> | null = null;
  const needReleaseForSt16 =
    req.contentOptions.convertSt16Iconsets && selected.some(isTheaterMix);
  const needReleaseForMovies = req.contentOptions.movieSequences;
  if (needReleaseForSt16 || needReleaseForMovies) {
    if (!req.release) {
      throw new Error(
        needReleaseForMovies
          ? 'Movie encoding requires the itch.io release ZIP with video/*.w16 sidecars'
          : 'ST16 iconset conversion requires the itch.io release ZIP with matching *.W16 files',
      );
    }
    const releaseAssets = await extractReleaseAssets(req.release.file);
    releaseFiles = releaseAssets.files;
  }

  if (req.contentOptions.convertSt16Iconsets) {
    const theaterMixes = selected.filter(isTheaterMix);
    if (theaterMixes.length > 0) {
      if (!releaseFiles) {
        throw new Error(
          'ST16 iconset conversion requires the itch.io release ZIP with matching *.W16 files',
        );
      }
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
    if (!releaseFiles) {
      throw new Error(
        'Movie encoding requires the itch.io release ZIP with video/*.w16 sidecars',
      );
    }
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

  const outputFiles = new Map<string, Uint8Array>();

  for (const base of selected) {
    progress = { ...progress, phase: 'extract', current: base };
    onProgress(progress);
    await tick();

    if (shouldMergeDualDisc(base) && gdiMap.has(base) && nodMap.has(base)) {
      const gdiLoc = gdiMap.get(base)!;
      const nodLoc = nodMap.get(base)!;
      const remixOptsPreview = remixOptionsForMix(base, req.contentOptions, releaseFiles);
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

      if (remixOpts.convertVqa) {
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
        );
        output = movies.output;
        stats = movies.stats;
        entries = movies.entries;
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
    } else {
      const loc = gdiMap.get(base) ?? nodMap.get(base);
      if (!loc) {
        progress = logLine(progress, 'warn', `Missing on both discs: ${base}`);
        progress = { ...progress, done: progress.done + 1 };
        onProgress(progress);
        await tick();
        continue;
      }

      const raw = await extractFile(loc.source.file, loc.lba, loc.size);
      progress = logLine(
        progress,
        'info',
        `Extracted ${base} from ${loc.source.label} (${raw.length} bytes)`,
      );
      onProgress(progress);
      await tick();

      progress = { ...progress, phase: 'remix', current: base };
      const remixOpts = remixOptionsForMix(base, req.contentOptions, releaseFiles);

      let output: Uint8Array;
      let stats: Awaited<ReturnType<typeof remixMixBytes>>['stats'];
      let entries: RemixEntry[];
      let shpxPool: Uint8Array | undefined;

      if (remixOpts.convertVqa) {
        progress = logLine(progress, 'info', 'Parallel VQA encode…');
        onProgress(progress);
        await tick();
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
        );
        output = movies.output;
        stats = movies.stats;
        entries = movies.entries;
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
    }

    progress = { ...progress, done: progress.done + 1 };
    onProgress(progress);
    await tick();
  }

  progress = { ...progress, phase: 'zip', current: undefined };
  onProgress(progress);
  await tick();

  if (req.release) {
    progress = logLine(
      progress,
      'info',
      `Adding Atari ST release files from ${req.release.file.name}…`,
    );
    onProgress(progress);
    await tick();

    const releaseAssets = await extractReleaseAssets(req.release.file);
    let skippedVideo = 0;
    for (const [name, data] of releaseAssets.files) {
      if (name.startsWith('video/')) {
        skippedVideo++;
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
  }

  const output = lowercaseFileMap(outputFiles);
  const zipBlob = buildZip(output);
  progress = logLine(
    progress,
    'info',
    `Created ZIP (${output.size} files, ${zipBlob.size} bytes)`,
  );
  progress = { ...progress, phase: 'done' };
  onProgress(progress);

  return {
    zipBlob,
    fileNames: [...output.keys()],
    files: output,
  };
}
