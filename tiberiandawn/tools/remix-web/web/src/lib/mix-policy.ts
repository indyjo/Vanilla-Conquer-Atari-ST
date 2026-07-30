import type { ContentOptions } from './types';

/** Uppercase MIX basename sets for optional content toggles. */
export const OPTIONAL_GROUPS = {
  speechAndSfx: ['SOUNDS.MIX', 'SPEECH.MIX'],
  musicScores: ['SCORES.MIX'],
  movieSequences: ['MOVIES.MIX'],
} as const;

/** Never extract these from the ISO (even if present). */
export const NEVER_EXTRACT = new Set(['SETUP.MIX', 'ZOUNDS.MIX', 'AUD.MIX']);

/** Core MIX files always included when present on disc. */
export const CORE_MIXES = new Set([
  'CONQUER.MIX',
  'GENERAL.MIX',
  'LOCAL.MIX',
  'TRANSIT.MIX',
  'TEMPERAT.MIX',
  'DESERT.MIX',
  'WINTER.MIX',
  'SNOW.MIX',
  'JUNGLE.MIX',
]);

/**
 * MIX archives that appear on both GDI and NOD discs and must be union-merged
 * (by CRC) before REMIX so shared payloads are only converted once.
 */
export const DUAL_DISC_MERGE_MIXES = new Set(['GENERAL.MIX', 'MOVIES.MIX']);

export function shouldMergeDualDisc(basename: string): boolean {
  return DUAL_DISC_MERGE_MIXES.has(basename.toUpperCase());
}

/** Flatten ISO path to output basename (uppercase). */
export function flattenMixPath(isoPath: string): string {
  const parts = isoPath.replace(/\\/g, '/').split('/');
  return parts[parts.length - 1].toUpperCase();
}

export function isMixFile(name: string): boolean {
  return name.toUpperCase().endsWith('.MIX');
}

export function shouldExtractMix(basename: string, options: ContentOptions): boolean {
  const upper = basename.toUpperCase();
  if (!isMixFile(upper)) return false;
  if (NEVER_EXTRACT.has(upper)) return false;

  if (CORE_MIXES.has(upper)) return true;
  if (upper.startsWith('UPDATE') || upper.startsWith('SC-')) return true;

  if (options.speechAndSfx && OPTIONAL_GROUPS.speechAndSfx.includes(upper as never))
    return true;
  if (options.musicScores && OPTIONAL_GROUPS.musicScores.includes(upper as never))
    return true;
  if (options.movieSequences && OPTIONAL_GROUPS.movieSequences.includes(upper as never))
    return true;

  return false;
}

export function listSelectedMixes(
  available: string[],
  options: ContentOptions,
): string[] {
  const seen = new Set<string>();
  const out: string[] = [];
  for (const path of available) {
    const base = flattenMixPath(path);
    if (!shouldExtractMix(base, options)) continue;
    if (seen.has(base)) continue;
    seen.add(base);
    out.push(base);
  }
  out.sort();
  return out;
}
