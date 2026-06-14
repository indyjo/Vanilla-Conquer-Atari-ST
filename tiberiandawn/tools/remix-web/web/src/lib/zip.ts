import { zipSync } from 'fflate';

/** Output names for Atari ST folders (TOS convention: lowercase). */
export function lowercaseFileMap(files: Map<string, Uint8Array>): Map<string, Uint8Array> {
  const out = new Map<string, Uint8Array>();
  for (const [name, data] of files) {
    out.set(name.toLowerCase(), data);
  }
  return out;
}

export function buildZip(files: Map<string, Uint8Array>): Blob {
  const entries: Record<string, Uint8Array> = {};
  for (const [name, data] of files) {
    entries[name.toLowerCase()] = data;
  }
  const zipped = zipSync(entries, { level: 6 });
  return new Blob([zipped], { type: 'application/zip' });
}

export function downloadBlob(blob: Blob, filename: string): void {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.click();
  URL.revokeObjectURL(url);
}
