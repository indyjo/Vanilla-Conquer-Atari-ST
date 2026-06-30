import { unzipSync } from 'fflate';

export class ReleaseZipError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'ReleaseZipError';
  }
}

/** Basenames to pull from an itch.io / Atari ST release ZIP into the output folder. */
function isReleaseAsset(basename: string): boolean {
  const lower = basename.toLowerCase();
  if (lower === 'cnc.tos') return true;
  if (lower.endsWith('.w16')) return true;
  if (lower === 'readme.txt' || lower === 'readme.md') return true;
  return false;
}

function shouldSkipReleaseAsset(basename: string): boolean {
  const lower = basename.toLowerCase();
  // remix-web replaces the on-ST remix step; omit remix.tos from bundled output.
  return lower === 'remix.tos';
}

export interface ReleaseAssets {
  files: Map<string, Uint8Array>;
  hasCncTos: boolean;
  w16Count: number;
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

  for (const [path, data] of Object.entries(entries)) {
    if (path.endsWith('/')) continue;
    const parts = path.replace(/\\/g, '/').split('/');
    const basename = parts[parts.length - 1];
    if (!basename || shouldSkipReleaseAsset(basename)) continue;
    if (!isReleaseAsset(basename)) continue;

    if (!files.has(basename.toLowerCase())) {
      files.set(basename.toLowerCase(), data);
    }

    const lower = basename.toLowerCase();
    if (lower === 'cnc.tos') hasCncTos = true;
    if (lower.endsWith('.w16')) w16Count++;
  }

  if (!hasCncTos) {
    throw new ReleaseZipError('Release ZIP must contain cnc.tos');
  }

  return { files, hasCncTos, w16Count };
}

export function describeReleaseAssets(assets: ReleaseAssets): string[] {
  return [...assets.files.keys()].sort((a, b) => a.localeCompare(b, undefined, { sensitivity: 'base' }));
}

export async function readReleaseReadme(zipFile: File): Promise<string | null> {
  const assets = await extractReleaseAssets(zipFile);
  const readme =
    assets.files.get('readme.txt') ?? assets.files.get('readme.md') ?? null;
  if (!readme) return null;
  return new TextDecoder().decode(readme);
}
