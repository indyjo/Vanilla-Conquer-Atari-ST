/** CONQUER.MIX basename eligible for SHPX KeyFrame conversion. */
export function isConquerMix(basename: string): boolean {
  return basename.toUpperCase() === 'CONQUER.MIX';
}

/** Sidecar pool filename for a given pool id (default 1 → pool0001.bin). */
export function shpxPoolBasename(poolId = 1): string {
  return `pool${poolId.toString(16).padStart(4, '0')}.bin`;
}
