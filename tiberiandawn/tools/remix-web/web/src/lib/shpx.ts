/** MIX basename → SHPX pool id (must match remix_shpx_default_pool_id). */
const SHPX_POOL_IDS: Record<string, number> = {
  'CONQUER.MIX': 0x0001,
  'TEMPERAT.MIX': 0x0002,
  'DESERT.MIX': 0x0003,
  'WINTER.MIX': 0x0004,
};

/** Pool id for an eligible MIX basename, or null if not SHPX-eligible. */
export function shpxPoolIdForMix(basename: string): number | null {
  return SHPX_POOL_IDS[basename.toUpperCase()] ?? null;
}

/** TRUE when basename is eligible for SHPX KeyFrame conversion. */
export function isShpxEligibleMix(basename: string): boolean {
  return shpxPoolIdForMix(basename) !== null;
}

/** Sidecar pool filename for a given pool id (default 1 → pool0001.bin). */
export function shpxPoolBasename(poolId = 1): string {
  return `pool${poolId.toString(16).padStart(4, '0')}.bin`;
}
