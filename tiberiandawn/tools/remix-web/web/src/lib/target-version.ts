import type { TargetVersion, TargetVersionState } from './types';

export function defaultSt16ForVersion(version: TargetVersion): boolean {
  return version === '0.2.x';
}

export function defaultTargetVersionState(): TargetVersionState {
  return { version: '0.2.x', source: 'manual' };
}

/** Parse C&C4ST version from itch.io release readme text. */
export function parseTargetVersionFromReadme(text: string): TargetVersion | null {
  const lower = text.toLowerCase();
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
    return 'Pre-converted ST16 iconsets require the next release. The first public beta expects standard 8bpp iconsets in theater MIX files.';
  }
  if (version === '0.2.x' && !enabled) {
    return 'Leaving iconsets unconverted moves ST16 work to mission start (slower load, higher RAM use on the next release).';
  }
  return null;
}

export const TARGET_VERSION_OPTIONS: { value: TargetVersion; label: string }[] = [
  { value: '0.1.x', label: 'First public beta (0.1.x)' },
  { value: '0.2.x', label: 'Next release (0.2.x)' },
];
