import type { RemixEntry } from './wasm-bridge';

export function formatRemixEntryLine(entry: RemixEntry): string {
  const crc = entry.crc.toString(16).padStart(8, '0').toUpperCase();
  if (entry.oldSize === 0) {
    return `${crc} empty`;
  }
  if (entry.typeIn !== entry.typeOut) {
    return `${crc} ${entry.oldSize} ${entry.typeIn} → ${entry.typeOut}`;
  }
  return `${crc} ${entry.oldSize} ${entry.typeIn}`;
}

/** Log lines for notable per-entry REMIX results (converted audio, etc.). */
export function notableEntryLines(entries: RemixEntry[], maxLines = 24): string[] {
  /* VQA→STV is logged when encode starts (live); skip those here to avoid duplicates. */
  const converted = entries.filter(
    (e) =>
      e.oldSize > 0 &&
      e.typeIn !== e.typeOut &&
      !(e.typeIn === 'vqa' && e.typeOut === 'stv'),
  );
  const lines = converted.slice(0, maxLines).map((e) => `  ${formatRemixEntryLine(e)}`);
  if (converted.length > maxLines) {
    lines.push(`  … ${converted.length - maxLines} more converted`);
  }
  return lines;
}

export function entrySummary(entries: RemixEntry[]): string {
  const payloads = entries.filter((e) => e.oldSize > 0);
  const converted = payloads.filter((e) => e.typeIn !== e.typeOut);
  return `${payloads.length} payloads, ${converted.length} converted, ${payloads.length - converted.length} unchanged`;
}
