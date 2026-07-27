/**
 * Plain Westwood MIX parse / assemble (matches remix_mix.c layout rules).
 */

export interface MixDirEntry {
  crc: number;
  /** Offset from data section start. */
  offset: number;
  size: number;
}

export interface ParsedMix {
  count: number;
  dataSize: number;
  dataStart: number;
  /** Directory order as stored (usually sorted by signed CRC). */
  entries: MixDirEntry[];
}

function readU16(view: DataView, off: number): number {
  return view.getUint16(off, true);
}

function readU32(view: DataView, off: number): number {
  return view.getUint32(off, true);
}

function writeU16(buf: Uint8Array, off: number, v: number): void {
  buf[off] = v & 0xff;
  buf[off + 1] = (v >> 8) & 0xff;
}

function writeU32(buf: Uint8Array, off: number, v: number): void {
  buf[off] = v & 0xff;
  buf[off + 1] = (v >> 8) & 0xff;
  buf[off + 2] = (v >> 16) & 0xff;
  buf[off + 3] = (v >> 24) & 0xff;
}

export function parseMix(data: Uint8Array): ParsedMix {
  if (data.length < 6) {
    throw new Error('MIX too small for header');
  }
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const count = readU16(view, 0);
  const dataSize = readU32(view, 2);
  if (count === 0) {
    throw new Error('MIX has zero entries');
  }
  const indexBytes = count * 12;
  const dataStart = 6 + indexBytes;
  if (data.length < dataStart) {
    throw new Error('MIX truncated before data section');
  }
  const entries: MixDirEntry[] = [];
  for (let i = 0; i < count; i++) {
    const off = 6 + i * 12;
    entries.push({
      crc: readU32(view, off),
      offset: readU32(view, off + 4),
      size: readU32(view, off + 8),
    });
  }
  return { count, dataSize, dataStart, entries };
}

export function extractMixPayload(mix: Uint8Array, parsed: ParsedMix, index: number): Uint8Array {
  const e = parsed.entries[index];
  if (!e) {
    throw new Error(`MIX entry index ${index} out of range`);
  }
  if (e.size === 0) {
    return new Uint8Array(0);
  }
  const start = parsed.dataStart + e.offset;
  const end = start + e.size;
  if (end > mix.length) {
    throw new Error(
      `MIX payload ${e.crc.toString(16)} truncated (${end} > ${mix.length})`,
    );
  }
  return mix.subarray(start, end);
}

/** FORM….WVQA → vqa; FORM….STVQ → stv; else null. */
export function detectVqaKind(payload: Uint8Array): 'vqa' | 'stv' | null {
  if (payload.length < 4) return null;
  if (payload[0] !== 0x46 || payload[1] !== 0x4f || payload[2] !== 0x52 || payload[3] !== 0x4d) {
    return null;
  }
  if (payload.length >= 12 && payload[8] === 0x53 && payload[9] === 0x54 && payload[10] === 0x56
    && payload[11] === 0x51) {
    return 'stv';
  }
  return 'vqa';
}

/**
 * Body order for reassembly: by ascending data offset (source payload order),
 * not directory/CRC order.
 */
export function bodyOrderIndices(parsed: ParsedMix): number[] {
  const indices = parsed.entries.map((_, i) => i);
  indices.sort((a, b) => {
    const ea = parsed.entries[a];
    const eb = parsed.entries[b];
    if (ea.offset !== eb.offset) return ea.offset - eb.offset;
    return a - b;
  });
  return indices;
}

export interface AssemblePayload {
  crc: number;
  /** null/undefined = omit from output. */
  payload: Uint8Array | null;
}

/** Signed int32 CRC compare — matches MixFileClass::compfunc / remix entry_cmp_crc. */
function cmpCrcSigned(a: number, b: number): number {
  const ca = a | 0;
  const cb = b | 0;
  if (ca < cb) return -1;
  if (ca > cb) return 1;
  return 0;
}

/**
 * Build a plain MIX from payloads in body (source) order.
 * Applies even-byte alignment before each payload; sorts the directory by signed CRC.
 */
export function assembleMix(items: AssemblePayload[]): Uint8Array {
  const kept = items.filter((it) => it.payload !== null);
  if (kept.length === 0) {
    throw new Error('assembleMix: no payloads to write');
  }

  const dataStart = 6 + kept.length * 12;
  const bodyParts: Uint8Array[] = [];
  let bodyPos = 0;
  const dir: { crc: number; offset: number; size: number }[] = [];

  for (const it of kept) {
    const payload = it.payload!;
    if (((dataStart + bodyPos) & 1) !== 0) {
      bodyParts.push(new Uint8Array([0]));
      bodyPos += 1;
    }
    dir.push({ crc: it.crc >>> 0, offset: bodyPos, size: payload.length });
    if (payload.length > 0) {
      bodyParts.push(payload);
      bodyPos += payload.length;
    }
  }

  dir.sort((a, b) => cmpCrcSigned(a.crc, b.crc));

  const out = new Uint8Array(dataStart + bodyPos);
  writeU16(out, 0, kept.length);
  writeU32(out, 2, bodyPos);
  for (let i = 0; i < dir.length; i++) {
    const off = 6 + i * 12;
    writeU32(out, off, dir[i].crc);
    writeU32(out, off + 4, dir[i].offset);
    writeU32(out, off + 8, dir[i].size);
  }
  let cursor = dataStart;
  for (const part of bodyParts) {
    out.set(part, cursor);
    cursor += part.length;
  }
  return out;
}
