/** Theater MIX basenames that contain terrain iconsets eligible for ST16 conversion. */
export const THEATER_MIXES = new Set([
  'TEMPERAT.MIX',
  'DESERT.MIX',
  'WINTER.MIX',
  'SNOW.MIX',
  'JUNGLE.MIX',
]);

/** Map theater MIX basename to C2P weight-set stem (filename without .W16). */
export function w16StemForTheaterMix(mixBasename: string): string | null {
  const upper = mixBasename.toUpperCase();
  switch (upper) {
    case 'TEMPERAT.MIX':
      return 'TEMPERAT';
    case 'DESERT.MIX':
      return 'DESERT';
    case 'WINTER.MIX':
    case 'SNOW.MIX':
      return 'WINTER';
    case 'JUNGLE.MIX':
      return 'JUNGLE';
    default:
      return null;
  }
}

export function isTheaterMix(mixBasename: string): boolean {
  return THEATER_MIXES.has(mixBasename.toUpperCase());
}

/** Required W16 stems for a set of theater MIX basenames (unique, sorted). */
export function requiredW16Stems(mixBasenames: string[]): string[] {
  const stems = new Set<string>();
  for (const base of mixBasenames) {
    const stem = w16StemForTheaterMix(base);
    if (stem) stems.add(stem);
  }
  return [...stems].sort();
}
