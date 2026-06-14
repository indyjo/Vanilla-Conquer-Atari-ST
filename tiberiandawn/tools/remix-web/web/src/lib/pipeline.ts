import { detectDiscLabel, extractFile, parseIso9660, type Iso9660Volume } from './iso9660';
import { flattenMixPath, listSelectedMixes } from './mix-policy';
import { buildZip, lowercaseFileMap } from './zip';
import { extractReleaseAssets } from './release-zip';
import { remixMergeMixBytes, remixMixBytes } from './wasm-bridge';
import { entrySummary, notableEntryLines } from './entry-log';
import type { ContentOptions, DiscSelection, PipelineResult, ProcessProgress, ReleaseSelection } from './types';

export interface PipelineRequest {
  gdi: DiscSelection;
  nod: DiscSelection;
  release?: ReleaseSelection | null;
  contentOptions: ContentOptions;
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

      const { output, stats, entries } = await remixMergeMixBytes(rawGdi, rawNod, req.wasmBaseUrl);
      progress = logLine(
        progress,
        'info',
        `Remixed ${base}: ${entrySummary(entries)} (${stats.audio_converted} audio converted)`,
      );
      for (const line of notableEntryLines(entries)) {
        progress = logLine(progress, 'info', line);
      }
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

      const { output, stats, entries } = await remixMixBytes(raw, req.wasmBaseUrl);
      progress = logLine(
        progress,
        'info',
        `Remixed ${base}: ${entrySummary(entries)} (${stats.audio_converted} audio converted)`,
      );
      for (const line of notableEntryLines(entries)) {
        progress = logLine(progress, 'info', line);
      }
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
