/** MIX basename → AUDX pool id (must match remix_audx_default_pool_id). */
const AUDX_POOL_BY_MIX: Record<string, number> = {
  'SOUNDS.MIX': 5,
  'SPEECH.MIX': 6,
  'SCORES.MIX': 7,
};

export function audxPoolIdForMix(basename: string): number | null {
  const id = AUDX_POOL_BY_MIX[basename.toUpperCase()];
  return id === undefined ? null : id;
}

export function isAudxEligibleMix(basename: string): boolean {
  return audxPoolIdForMix(basename) !== null;
}

export function audxPoolBasename(poolId: number): string {
  return `pool${poolId.toString(16).padStart(4, '0')}.bin`;
}
