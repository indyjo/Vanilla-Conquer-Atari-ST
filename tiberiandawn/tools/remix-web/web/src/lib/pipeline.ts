import { detectDiscLabel, extractFile, parseIso9660, type Iso9660Volume } from './iso9660';
import { flattenMixPath, listSelectedMixes } from './mix-policy';
import { buildZip, lowercaseFileMap } from './zip';
import { extractReleaseAssets } from './release-zip';
import { remixMergeMixBytes, remixMixBytes, type RemixEntry, type RemixMixOptions } from './wasm-bridge';
import { entrySummary, notableEntryLines } from './entry-log';
import { isTheaterMix, requiredW16Stems, w16StemForTheaterMix } from './theater-st16';
import type { ContentOptions, DiscSelection, PipelineResult, ProcessProgress, ReleaseSelection, TargetVersion } from './types';

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
  },
): string {
  let line = `Remixed ${base}: ${entrySummary(entries)} (${stats.audio_converted} audio converted`;
  if (stats.iconset_converted > 0 || stats.iconset_already_st16 > 0) {
    line += `, ${stats.iconset_converted} iconset ST16, ${stats.iconset_already_st16} already ST16`;
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
  convertSt16: boolean,
  releaseFiles: Map<string, Uint8Array> | null,
): RemixMixOptions {
  const theater = isTheaterMix(mixBasename);
  const enabled = convertSt16 && theater;
  let w16Bytes: Uint8Array | undefined;
  if (enabled) {
    if (!releaseFiles) {
      throw new Error(
        'ST16 iconset conversion requires the itch.io release ZIP with matching *.W16 files',
      );
    }
    w16Bytes = w16BytesForMix(mixBasename, releaseFiles);
  }
  return {
    convertSt16Iconsets: enabled,
    mixBasename,
    w16Bytes,
  };
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
  if (req.contentOptions.convertSt16Iconsets) {
    const theaterMixes = selected.filter(isTheaterMix);
    if (theaterMixes.length > 0) {
      if (!req.release) {
        throw new Error(
          'ST16 iconset conversion requires the itch.io release ZIP with matching *.W16 files',
        );
      }
      const releaseAssets = await extractReleaseAssets(req.release.file);
      releaseFiles = releaseAssets.files;
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

  const outputFiles = new Map<string, Uint8Array>();

  for (const base of selected) {
    progress = { ...progress, phase: 'extract', current: base };
    onProgress(progress);
    await tick();

    if (base === 'GENERAL.MIX' && gdiMap.has(base) && nodMap.has(base)) {
      const gdiLoc = gdiMap.get(base)!;
      const nodLoc = nodMap.get(base)!;
      progress = logLine(progress, 'info', 'Merging GENERAL.MIX from GDI + NOD');
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
      progress = logLine(progress, 'info', 'Merge + REMIX (WebAssembly)…');
      onProgress(progress);
      await tick();

      const remixOpts = remixOptionsForMix(base, req.contentOptions.convertSt16Iconsets, releaseFiles);
      const inputBytes = Math.max(rawGdi.length, rawNod.length);
      const { output, stats, entries } = await remixMergeMixBytes(
        rawGdi,
        rawNod,
        req.wasmBaseUrl,
        remixOpts,
      );
      progress = logLine(progress, 'info', remixLogLine(base, entries, stats));
      for (const line of notableEntryLines(entries)) {
        progress = logLine(progress, 'info', line);
      }
      progress = logLine(progress, 'info', savedLogLine(base, inputBytes, output.length));
      outputFiles.set(base, output);
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
      progress = logLine(progress, 'info', 'REMIX (WebAssembly)…');
      onProgress(progress);
      await tick();

      const remixOpts = remixOptionsForMix(base, req.contentOptions.convertSt16Iconsets, releaseFiles);
      const { output, stats, entries } = await remixMixBytes(raw, req.wasmBaseUrl, remixOpts);
      progress = logLine(progress, 'info', remixLogLine(base, entries, stats));
      for (const line of notableEntryLines(entries)) {
        progress = logLine(progress, 'info', line);
      }
      progress = logLine(progress, 'info', savedLogLine(base, raw.length, output.length));
      outputFiles.set(base, output);
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
    for (const [name, data] of releaseAssets.files) {
      if (outputFiles.has(name)) {
        progress = logLine(progress, 'warn', `Skipped release file (already in output): ${name}`);
      } else {
        outputFiles.set(name, data);
        progress = logLine(progress, 'info', `Included ${name} (${data.length} bytes)`);
      }
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
