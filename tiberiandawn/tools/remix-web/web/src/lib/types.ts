export type DiscLabel = 'GDI' | 'NOD';

export interface ContentOptions {
  speechAndSfx: boolean;
  musicScores: boolean;
  movieSequences: boolean;
}

export const DEFAULT_CONTENT_OPTIONS: ContentOptions = {
  speechAndSfx: true,
  musicScores: false,
  movieSequences: false,
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

/** Optional itch.io / Atari ST release ZIP (cnc.tos + *.W16). */
export interface ReleaseSelection {
  file: File;
  /** Basenames that will be copied into the output ZIP. */
  assetNames: string[];
}

export type WizardStep = 'discs' | 'customize' | 'process' | 'checkout';
