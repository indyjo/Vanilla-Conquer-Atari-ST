import type { TargetVersion, TargetVersionState } from './types';

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
  return { version: '0.2.x', source: 'manual' };
}

/** Parse C&C4ST version from itch.io release readme text. */
export function parseTargetVersionFromReadme(text: string): TargetVersion | null {
  const lower = text.toLowerCase();
  if (/\b0\.3\.\d+\b/.test(lower) || lower.includes('0.3.x')) {
    return '0.3.x';
  }
  if (/\b0\.2\.\d+\b/.test(lower) || lower.includes('0.2.x')) {
    return '0.2.x';
  }
  if (/\b0\.1\.\d+\b/.test(lower) || lower.includes('0.1.x')) {
    return '0.1.x';
  }
  return null;
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
  { value: '0.2.x', label: 'Current release (0.2.x)' },
  { value: '0.3.x', label: 'FMV release (0.3.x)' },
];

export const VIDEO_QUALITY_OPTIONS: { value: 'low' | 'medium' | 'high'; label: string }[] = [
  { value: 'low', label: 'Low (suited for 8 MHz Atari ST)' },
  { value: 'medium', label: 'Medium' },
  { value: 'high', label: 'High (larger files)' },
];

export const VIDEO_EFFORT_OPTIONS: { value: 'fast' | 'normal' | 'thorough'; label: string }[] = [
  { value: 'fast', label: 'Fast' },
  { value: 'normal', label: 'Normal' },
  { value: 'thorough', label: 'Thorough (slower, better)' },
];

export const VIDEO_PARALLELISM_OPTIONS: { value: 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8; label: string }[] = [
  { value: 1, label: '1 worker' },
  { value: 2, label: '2 workers' },
  { value: 3, label: '3 workers' },
  { value: 4, label: '4 workers (default)' },
  { value: 5, label: '5 workers' },
  { value: 6, label: '6 workers' },
  { value: 7, label: '7 workers' },
  { value: 8, label: '8 workers' },
];

export function clampVideoParallelism(n: number): 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 {
  const v = Math.round(n);
  if (v < 1) return 1;
  if (v > 8) return 8;
  return v as 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8;
}
