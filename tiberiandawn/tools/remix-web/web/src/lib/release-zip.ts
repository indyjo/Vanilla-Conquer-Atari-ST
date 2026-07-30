import { unzipSync } from 'fflate';

export class ReleaseZipError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'ReleaseZipError';
  }
}

/** Basenames / paths to pull from an itch.io / Atari ST release ZIP into the output folder. */
function isReleaseAsset(relPath: string): boolean {
  const lower = relPath.toLowerCase();
  const basename = lower.includes('/') ? lower.slice(lower.lastIndexOf('/') + 1) : lower;
  if (basename === 'cnc.tos') return true;
  if (basename === 'record.bin') return true;
  if (basename.endsWith('.w16')) return true;
  return false;
}

export interface ReleaseAssets {
  /** Keys: lowercase basename for flat assets, or `video/xxxxxxxx.n.w16` for FMV sidecars. */
  files: Map<string, Uint8Array>;
  hasCncTos: boolean;
  w16Count: number;
  videoW16Count: number;
}

export async function extractReleaseAssets(zipFile: File): Promise<ReleaseAssets> {
  const buf = new Uint8Array(await zipFile.arrayBuffer());
  let entries: Record<string, Uint8Array>;
  try {
    entries = unzipSync(buf);
  } catch {
    throw new ReleaseZipError('Could not read ZIP — select the Atari ST release archive');
  }

  const files = new Map<string, Uint8Array>();
  let hasCncTos = false;
  let w16Count = 0;
  let videoW16Count = 0;

  for (const [path, data] of Object.entries(entries)) {
    if (path.endsWith('/')) continue;
    const parts = path.replace(/\\/g, '/').split('/');
    const basename = parts[parts.length - 1];
    if (!basename) continue;

    const parent = parts.length >= 2 ? parts[parts.length - 2].toLowerCase() : '';
    const lowerBase = basename.toLowerCase();

    if (parent === 'video' && lowerBase.endsWith('.w16')) {
      const key = `video/${lowerBase}`;
      if (!files.has(key)) {
        files.set(key, data);
        videoW16Count++;
      }
      continue;
    }

    if (!isReleaseAsset(lowerBase)) continue;

    if (!files.has(lowerBase)) {
      files.set(lowerBase, data);
    }

    if (lowerBase === 'cnc.tos') hasCncTos = true;
    if (lowerBase.endsWith('.w16')) w16Count++;
  }

  if (!hasCncTos) {
    throw new ReleaseZipError('Release ZIP must contain cnc.tos');
  }

  return { files, hasCncTos, w16Count, videoW16Count };
}

export function describeReleaseAssets(assets: ReleaseAssets): string[] {
  return [...assets.files.keys()].sort((a, b) => a.localeCompare(b, undefined, { sensitivity: 'base' }));
}
