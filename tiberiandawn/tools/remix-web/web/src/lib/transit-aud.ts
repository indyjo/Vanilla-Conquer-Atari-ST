/**
 * Move classic AUD payloads from TRANSIT.MIX into SOUNDS.MIX before AUDX rempack.
 *
 * TRANSIT cannot be MFCD::Cache()'d (CHOOSE.WSA / VQA / RECORD.BIN). AUDX metas
 * belong in the already-cached SOUNDS.MIX; sample bytes go to pool0005.bin.
 */

import {
  assembleMix,
  bodyOrderIndices,
  extractMixPayload,
  parseMix,
  type AssemblePayload,
} from './mix-format';

const AUD_HDR_LEN = 12;
const AUD_COMP_PCM = 0;
const AUD_COMP_WESTWOOD = 1;
const AUD_COMP_IMA99 = 99;
const AUD99_MAX_COMPRESSED = 16 * 1024 * 1024;
const AUD99_MAX_DECODED = 32 * 1024 * 1024;
const AUD99_FRAME_MAGIC = 0x0000deaf;

function readU32(data: Uint8Array, off: number): number {
  return (
    data[off]! |
    (data[off + 1]! << 8) |
    (data[off + 2]! << 16) |
    (data[off + 3]! << 24)
  ) >>> 0;
}

function readU16(data: Uint8Array, off: number): number {
  return data[off]! | (data[off + 1]! << 8);
}

/** True for classic LE AUD (IMA99 / Westwood / PCM); false for AUDX meta. */
export function looksLikeAud(data: Uint8Array): boolean {
  if (data.length < AUD_HDR_LEN) return false;
  /* AUDX magic 'AUDX' as BE — never treat as classic AUD. */
  if (
    data[0] === 0x41 &&
    data[1] === 0x55 &&
    data[2] === 0x44 &&
    data[3] === 0x58
  ) {
    return false;
  }

  const compression = data[11]!;
  const compSize = readU32(data, 2);
  const uncomp = readU32(data, 6);
  const payloadAvail = data.length > AUD_HDR_LEN ? data.length - AUD_HDR_LEN : 0;

  if (compSize === 0) return false;
  if (payloadAvail !== 0 && compSize !== payloadAvail) return false;
  if (compSize > AUD99_MAX_COMPRESSED) return false;
  if (uncomp === 0 || uncomp > AUD99_MAX_DECODED) return false;

  if (compression === AUD_COMP_IMA99) {
    if (compSize < 8) return false;
    if (data.length >= AUD_HDR_LEN + 8) {
      const decomp = readU16(data, AUD_HDR_LEN + 2);
      const magic = readU32(data, AUD_HDR_LEN + 4);
      if (magic !== AUD99_FRAME_MAGIC || decomp === 0 || (decomp & 1) !== 0) return false;
    }
    return true;
  }
  if (compression === AUD_COMP_WESTWOOD) return true;
  if (compression === AUD_COMP_PCM) return compSize === uncomp;
  return false;
}

export interface TransitAudHarvest {
  /** TRANSIT.MIX without AUD entries (or original if none found). */
  stripped: Uint8Array;
  /** Classic AUD payloads to append into SOUNDS.MIX (body order). */
  auds: AssemblePayload[];
  /** Total bytes of harvested AUD payloads. */
  audBytes: number;
}

/**
 * Split TRANSIT.MIX into non-audio remainder + AUD entries (copied payloads).
 */
export function harvestTransitAuds(transitMix: Uint8Array): TransitAudHarvest {
  const parsed = parseMix(transitMix);
  const auds: AssemblePayload[] = [];
  const rest: AssemblePayload[] = [];
  let audBytes = 0;

  for (const idx of bodyOrderIndices(parsed)) {
    const e = parsed.entries[idx]!;
    const payload = new Uint8Array(extractMixPayload(transitMix, parsed, idx));
    if (looksLikeAud(payload)) {
      auds.push({ crc: e.crc >>> 0, payload });
      audBytes += payload.length;
    } else {
      rest.push({ crc: e.crc >>> 0, payload });
    }
  }

  if (auds.length === 0) {
    return { stripped: transitMix, auds: [], audBytes: 0 };
  }
  if (rest.length === 0) {
    throw new Error('TRANSIT.MIX would be empty after moving all AUD entries');
  }

  return {
    stripped: assembleMix(rest),
    auds,
    audBytes,
  };
}

/**
 * Append harvested AUD payloads onto SOUNDS.MIX (skip CRCs already present).
 */
export function injectAudsIntoMix(mix: Uint8Array, auds: AssemblePayload[]): {
  mix: Uint8Array;
  added: number;
  skipped: number;
} {
  if (auds.length === 0) {
    return { mix, added: 0, skipped: 0 };
  }

  const parsed = parseMix(mix);
  const existing = new Set(parsed.entries.map((e) => e.crc >>> 0));
  const items: AssemblePayload[] = [];

  for (const idx of bodyOrderIndices(parsed)) {
    const e = parsed.entries[idx]!;
    items.push({
      crc: e.crc >>> 0,
      payload: new Uint8Array(extractMixPayload(mix, parsed, idx)),
    });
  }

  let added = 0;
  let skipped = 0;
  for (const aud of auds) {
    const crc = aud.crc >>> 0;
    if (existing.has(crc)) {
      skipped++;
      continue;
    }
    items.push({ crc, payload: aud.payload });
    existing.add(crc);
    added++;
  }

  if (added === 0) {
    return { mix, added: 0, skipped };
  }
  return { mix: assembleMix(items), added, skipped };
}
