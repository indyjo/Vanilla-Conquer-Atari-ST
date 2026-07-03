export type DiscLabel = 'GDI' | 'NOD';

export type TargetVersion = '0.1.x' | '0.2.x';

export interface TargetVersionState {
  version: TargetVersion;
  source: 'release' | 'manual';
}

export interface ContentOptions {
  speechAndSfx: boolean;
  musicScores: boolean;
  movieSequences: boolean;
  convertSt16Iconsets: boolean;
  convertShpx: boolean;
}

export const DEFAULT_CONTENT_OPTIONS: ContentOptions = {
  speechAndSfx: true,
  musicScores: false,
  movieSequences: false,
  convertSt16Iconsets: true,
  convertShpx: true,
};

export interface IsoFileEntry {
  name: string;
  lba: number;
  size: number;
}

export interface ParsedIso {
  label: DiscLabel;
  volumeId: string;
  entries: IsoFileEntry[];
}

export interface ProcessLogLine {
  level: 'info' | 'warn' | 'error';
  text: string;
}

export interface ProcessProgress {
  phase: 'idle' | 'scan' | 'extract' | 'remix' | 'zip' | 'done' | 'error';
  current?: string;
  done: number;
  total: number;
  log: ProcessLogLine[];
}

export interface PipelineResult {
  zipBlob: Blob;
  fileNames: string[];
  files: Map<string, Uint8Array>;
}

export interface CheckoutPayload {
  zipBlob: Blob;
  files: Map<string, Uint8Array>;
  fileNames: string[];
  log: ProcessLogLine[];
}

export interface DiscSelection {
  file: File;
  label: DiscLabel;
  volumeId: string;
  /** Original ZIP filename when the disc was extracted from a wrapper archive. */
  sourceZip?: string;
}

/** itch.io / Atari ST release ZIP (cnc.tos + *.W16) — recommended for ST16 terrain conversion. */
export interface ReleaseSelection {
  file: File;
  /** Basenames that will be copied into the output ZIP. */
  assetNames: string[];
}

export type WizardStep = 'discs' | 'customize' | 'process' | 'checkout';
