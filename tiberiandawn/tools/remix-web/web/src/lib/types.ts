export type DiscLabel = 'GDI' | 'NOD';

export type TargetVersion = '0.1.x' | '0.2.x' | '0.3.x';

export type VideoQuality = 'low' | 'medium' | 'high';

export type VideoEffort = 'fast' | 'normal' | 'thorough';

/** Parallel VQA encode workers (1–8). */
export type VideoParallelism = 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8;

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
  videoQuality: VideoQuality;
  videoEffort: VideoEffort;
  videoParallelism: VideoParallelism;
}

export const DEFAULT_CONTENT_OPTIONS: ContentOptions = {
  speechAndSfx: true,
  musicScores: true,
  movieSequences: true,
  convertSt16Iconsets: true,
  convertShpx: true,
  videoQuality: 'medium',
  videoEffort: 'normal',
  videoParallelism: 4,
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

export interface EncodeJobProgress {
  phase: string;
  label: string;
  done: number;
  total: number;
}

export interface ProcessProgress {
  phase: 'idle' | 'scan' | 'extract' | 'remix' | 'zip' | 'done' | 'error';
  current?: string;
  done: number;
  total: number;
  log: ProcessLogLine[];
  /** Live VQA→STVQ encode jobs (one entry per in-flight CRC). */
  encodes?: EncodeJobProgress[];
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

/** itch.io / Atari ST release ZIP (cnc.tos + record.bin + *.W16) — required input. */
export interface ReleaseSelection {
  file: File;
  /** Basenames that will be copied into the output ZIP. */
  assetNames: string[];
}

export type WizardStep = 'discs' | 'customize' | 'process' | 'checkout';
