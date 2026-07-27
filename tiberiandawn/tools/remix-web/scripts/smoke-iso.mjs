#!/usr/bin/env node
/** Quick smoke test: ISO9660 parse + volume label on a user ISO. */
import { open } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';

// Minimal File-like wrapper for Node
class NodeFile {
  constructor(name, buffer) {
    this.name = name;
    this.size = buffer.byteLength;
    this._buf = buffer;
  }
  slice(start, end) {
    const sub = this._buf.subarray(start, end ?? this.size);
    const copy = sub.buffer.slice(sub.byteOffset, sub.byteOffset + sub.byteLength);
    return {
      arrayBuffer: async () => copy,
    };
  }
}

const isoPath = process.argv[2] ?? `${process.env.HOME}/Downloads/DOSCNC_GDI.iso`;
const buf = await open(isoPath).then(async (h) => {
  const st = await h.stat();
  const b = Buffer.alloc(st.size);
  await h.read(b, 0, st.size, 0);
  await h.close();
  return b;
});

const { parseIso9660, detectDiscLabel } = await import(
  '../web/src/lib/iso9660.ts'
);
const { listSelectedMixes } = await import('../web/src/lib/mix-policy.ts');

const file = new NodeFile(isoPath, buf);
const vol = await parseIso9660(file);
const label = detectDiscLabel(vol.volumeId);
const mixes = vol.entries.filter((e) => e.path.toUpperCase().endsWith('.MIX')).map((e) => e.path);
const selected = listSelectedMixes(mixes, {
  speechAndSfx: true,
  musicScores: false,
  movieSequences: false,
  convertSt16Iconsets: true,
  convertShpx: true,
  videoQuality: 'medium',
  videoEffort: 'normal',
});

console.log('volumeId:', vol.volumeId, '->', label);
console.log('MIX paths on disc:', mixes.length);
console.log('Selected for default options:', selected.length);
selected.forEach((m) => console.log(' ', m));
