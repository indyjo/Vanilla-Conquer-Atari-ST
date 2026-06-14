import { unzipSync } from 'fflate';

export class DiscZipError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'DiscZipError';
  }
}

const ISO_EXTENSIONS = ['.iso', '.bin', '.img'] as const;

function basename(path: string): string {
  const parts = path.replace(/\\/g, '/').split('/');
  return parts[parts.length - 1] ?? '';
}

function isIgnorableZipPath(path: string): boolean {
  const norm = path.replace(/\\/g, '/');
  if (path.endsWith('/')) return true;
  if (norm.startsWith('__MACOSX/')) return true;
  const base = basename(path);
  if (base === '.DS_Store') return true;
  return false;
}

export function isIsoExtension(name: string): boolean {
  const lower = name.toLowerCase();
  return ISO_EXTENSIONS.some((ext) => lower.endsWith(ext));
}

export function isDiscZip(name: string): boolean {
  return name.toLowerCase().endsWith('.zip');
}

/**
 * Intentionally omitted — macOS/WebKit file dialogs exclude ZIP when mixed with
 * .iso in accept= (see WebKit showOpenFilePicker). Validation is in resolveDiscFile().
 */
export const DISC_PICKER_ACCEPT: string | undefined = undefined;

/** Extract the sole ISO/BIN/IMG from a ZIP; error if zero or multiple images. */
export async function extractIsoFromZip(zipFile: File): Promise<File> {
  const buf = new Uint8Array(await zipFile.arrayBuffer());
  let entries: Record<string, Uint8Array>;
  try {
    entries = unzipSync(buf);
  } catch {
    throw new DiscZipError('Could not read ZIP archive');
  }

  const isoPaths: string[] = [];
  for (const path of Object.keys(entries)) {
    if (isIgnorableZipPath(path)) continue;
    if (isIsoExtension(basename(path))) {
      isoPaths.push(path);
    }
  }

  if (isoPaths.length === 0) {
    throw new DiscZipError('ZIP does not contain an ISO, BIN, or IMG file');
  }
  if (isoPaths.length > 1) {
    const names = isoPaths.map((p) => basename(p)).join(', ');
    throw new DiscZipError(`ZIP must contain exactly one disc image (found: ${names})`);
  }

  const isoPath = isoPaths[0];
  const data = entries[isoPath];
  const isoName = basename(isoPath);
  return new File([data], isoName, {
    type: 'application/octet-stream',
    lastModified: zipFile.lastModified,
  });
}

export interface ResolvedDiscFile {
  file: File;
  /** Picker filename when the disc was unpacked from a ZIP. */
  sourceZip?: string;
}

/** Accept a raw disc image or a ZIP wrapping a single disc image. */
export async function resolveDiscFile(picked: File): Promise<ResolvedDiscFile> {
  if (isDiscZip(picked.name)) {
    const file = await extractIsoFromZip(picked);
    return { file, sourceZip: picked.name };
  }
  if (isIsoExtension(picked.name)) {
    return { file: picked };
  }
  throw new DiscZipError('Select an ISO, BIN, IMG, or ZIP containing one disc image');
}
