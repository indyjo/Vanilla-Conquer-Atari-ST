const SECTOR_SIZE = 2048;
const PVD_SECTOR = 16;

export class Iso9660Error extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'Iso9660Error';
  }
}

function readUint16LE(view: DataView, offset: number): number {
  return view.getUint16(offset, true);
}

function readUint32LE(view: DataView, offset: number): number {
  return view.getUint32(offset, true);
}

function readUint32Both(view: DataView, offset: number): number {
  const le = readUint32LE(view, offset);
  const be = view.getUint32(offset + 4, false);
  if (le === be) return le;
  if (be === 0) return le;
  if (le === 0) return be;
  throw new Iso9660Error(`LBA/endian mismatch at offset ${offset}`);
}

function trimIsoString(bytes: Uint8Array): string {
  let end = bytes.length;
  while (end > 0 && bytes[end - 1] === 0x20) end--;
  return new TextDecoder('ascii').decode(bytes.subarray(0, end)).trim();
}

function parseDirLba(view: DataView, offset: number): number {
  return readUint32Both(view, offset + 2);
}

function parseDirSize(view: DataView, offset: number): number {
  return readUint32Both(view, offset + 10);
}

function parseDirEntryName(record: DataView, offset: number, len: number): string {
  const nameLen = record.getUint8(offset + 32);
  const nameStart = offset + 33;
  if (nameLen === 1) {
    const code = record.getUint8(nameStart);
    if (code === 0) return '.';
    if (code === 1) return '..';
  }
  const nameBytes = new Uint8Array(record.buffer, record.byteOffset + nameStart, nameLen);
  let name = new TextDecoder('ascii').decode(nameBytes);
  const semi = name.indexOf(';');
  if (semi >= 0) name = name.slice(0, semi);
  return name;
}

async function readSector(file: File, lba: number): Promise<ArrayBuffer> {
  const start = lba * SECTOR_SIZE;
  const end = start + SECTOR_SIZE;
  if (end > file.size) {
    throw new Iso9660Error(`Sector ${lba} extends past end of file`);
  }
  return file.slice(start, end).arrayBuffer();
}

export interface Iso9660Entry {
  path: string;
  lba: number;
  size: number;
  isDir: boolean;
}

export interface Iso9660Volume {
  volumeId: string;
  rootLba: number;
  rootSize: number;
  entries: Iso9660Entry[];
}

export async function parseIso9660(file: File): Promise<Iso9660Volume> {
  const pvdBuf = await readSector(file, PVD_SECTOR);
  const pvd = new DataView(pvdBuf);
  const type = pvd.getUint8(0);
  const id = trimIsoString(new Uint8Array(pvdBuf, 1, 5));
  if (type !== 1 || id !== 'CD001') {
    throw new Iso9660Error('Not a valid ISO9660 primary volume descriptor');
  }

  const volumeId = trimIsoString(new Uint8Array(pvdBuf, 40, 32));
  const rootLba = parseDirLba(pvd, 156);
  const rootSize = parseDirSize(pvd, 156);

  const entries: Iso9660Entry[] = [];
  await walkDirectory(file, '', rootLba, rootSize, entries);
  return { volumeId, rootLba, rootSize, entries };
}

async function walkDirectory(
  file: File,
  prefix: string,
  lba: number,
  size: number,
  out: Iso9660Entry[],
): Promise<void> {
  let remaining = size;
  let sector = lba;

  while (remaining > 0) {
    const buf = await readSector(file, sector);
    const view = new DataView(buf);
    let offset = 0;

    while (offset < SECTOR_SIZE) {
      const recLen = view.getUint8(offset);
      if (recLen === 0) break;

      const flags = view.getUint8(offset + 25);
      const name = parseDirEntryName(view, offset, recLen);
      const entryLba = parseDirLba(view, offset);
      const entrySize = parseDirSize(view, offset);
      const isDir = (flags & 0x02) !== 0;

      if (name !== '.' && name !== '..') {
        const path = prefix ? `${prefix}/${name}` : name;
        if (isDir) {
          await walkDirectory(file, path, entryLba, entrySize, out);
        } else {
          out.push({ path, lba: entryLba, size: entrySize, isDir: false });
        }
      }

      offset += recLen;
    }

    remaining = remaining > SECTOR_SIZE ? remaining - SECTOR_SIZE : 0;
    sector++;
  }
}

export function detectDiscLabel(volumeId: string): 'GDI' | 'NOD' | null {
  const upper = volumeId.trim().toUpperCase();
  if (upper === 'GDI') return 'GDI';
  if (upper === 'NOD') return 'NOD';
  return null;
}

export async function extractFile(file: File, lba: number, size: number): Promise<Uint8Array> {
  const start = lba * SECTOR_SIZE;
  const end = start + size;
  if (end > file.size) {
    throw new Iso9660Error(`File at LBA ${lba} (${size} bytes) extends past ISO end`);
  }
  const buf = await file.slice(start, end).arrayBuffer();
  return new Uint8Array(buf);
}

export { SECTOR_SIZE };
