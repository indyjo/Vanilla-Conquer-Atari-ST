import type { TargetVersion, TargetVersionState, VideoParallelism } from './types';

export function defaultSt16ForVersion(version: TargetVersion): boolean {
  return version === '0.2.x' || version === '0.3.x';
}

export function defaultShpxForVersion(version: TargetVersion): boolean {
  return version === '0.2.x' || version === '0.3.x';
}

export function moviesSupportedForVersion(version: TargetVersion): boolean {
  return version === '0.3.x';
}

export function defaultTargetVersionState(): TargetVersionState {
  return { version: '0.3.x', source: 'manual' };
}

function matchTargetVersion(text: string): TargetVersion | null {
  const lower = text.toLowerCase();
  if (/\b0\.3(\.\d+|\.x)?\b/.test(lower)) return '0.3.x';
  if (/\b0\.2(\.\d+|\.x)?\b/.test(lower)) return '0.2.x';
  if (/\b0\.1(\.\d+|\.x)?\b/.test(lower)) return '0.1.x';
  return null;
}

/** Guess target version from release ZIP filename (e.g. cncst-0.3.0-dev.zip); default 0.3.x. */
export function guessTargetVersionFromFilename(name: string): TargetVersion {
  return matchTargetVersion(name) ?? '0.3.x';
}

export function st16IncompatibilityWarning(version: TargetVersion, enabled: boolean): string | null {
  if (version === '0.1.x' && enabled) {
    return 'Pre-converted ST16 iconsets require the current release. The first public beta expects standard 8bpp iconsets in theater MIX files.';
  }
  if ((version === '0.2.x' || version === '0.3.x') && !enabled) {
    return 'Leaving iconsets unconverted moves ST16 work to mission start (slower load, higher RAM use on the current release).';
  }
  return null;
}

export function shpxIncompatibilityWarning(version: TargetVersion, enabled: boolean): string | null {
  if (version === '0.1.x' && enabled) {
    return 'SHPX repacked MIX files are incompatible with the 0.1.x line of C&C4ST. Use standard KeyFrame SHPs or target 0.2.x / 0.3.x.';
  }
  return null;
}

export const TARGET_VERSION_OPTIONS: { value: TargetVersion; label: string }[] = [
  { value: '0.1.x', label: 'First public beta (0.1.x)' },
  { value: '0.2.x', label: 'Previous release (0.2.x)' },
  { value: '0.3.x', label: 'Current release (0.3.x)' },
];

export const VIDEO_QUALITY_OPTIONS: { value: 'low' | 'medium' | 'high'; label: string }[] = [
  { value: 'low', label: 'Low' },
  { value: 'medium', label: 'Medium' },
  { value: 'high', label: 'High (larger files)' },
];

export const VIDEO_EFFORT_OPTIONS: { value: 'fast' | 'normal' | 'thorough'; label: string }[] = [
  { value: 'fast', label: 'Fast' },
  { value: 'normal', label: 'Normal' },
  { value: 'thorough', label: 'Thorough (slower, better)' },
];

export const VIDEO_PARALLELISM_VALUES: readonly VideoParallelism[] = [1, 2, 4, 6, 8, 12, 16];

export const VIDEO_PARALLELISM_OPTIONS: { value: VideoParallelism; label: string }[] = [
  { value: 1, label: '1 worker' },
  { value: 2, label: '2 workers' },
  { value: 4, label: '4 workers' },
  { value: 6, label: '6 workers (default)' },
  { value: 8, label: '8 workers' },
  { value: 12, label: '12 workers' },
  { value: 16, label: '16 workers' },
];

export function clampVideoParallelism(n: number): VideoParallelism {
  const v = Math.round(n);
  let best: VideoParallelism = VIDEO_PARALLELISM_VALUES[0]!;
  let bestDist = Math.abs(v - best);
  for (const opt of VIDEO_PARALLELISM_VALUES) {
    const dist = Math.abs(v - opt);
    if (dist < bestDist) {
      best = opt;
      bestDist = dist;
    }
  }
  return best;
}
