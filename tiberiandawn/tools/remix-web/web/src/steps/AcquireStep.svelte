<script lang="ts">
  import type { DiscLabel, DiscSelection, ReleaseSelection } from '../lib/types';
  import { DISC_PICKER_ACCEPT, resolveDiscFile } from '../lib/disc-zip';

  interface Props {
    gdi: DiscSelection | null;
    nod: DiscSelection | null;
    release: ReleaseSelection | null;
    onGdi: (disc: DiscSelection | null) => void;
    onNod: (disc: DiscSelection | null) => void;
    onRelease: (release: ReleaseSelection | null) => void;
    onNext: () => void;
  }

  let { gdi, nod, release, onGdi, onNod, onRelease, onNext }: Props = $props();

  let gdiError = $state('');
  let nodError = $state('');
  let releaseError = $state('');
  let busy = $state(false);

  async function handleDisc(input: HTMLInputElement, expected: DiscLabel) {
    const picked = input.files?.[0];
    if (!picked) return;

    busy = true;
    if (expected === 'GDI') gdiError = '';
    else nodError = '';

    try {
      const { parseIso9660, detectDiscLabel } = await import('../lib/iso9660');
      const { file, sourceZip } = await resolveDiscFile(picked);
      const volume = await parseIso9660(file);
      const label = detectDiscLabel(volume.volumeId);
      if (label !== expected) {
        throw new Error(
          `Expected ${expected} install disc (volume label ${expected}), got "${volume.volumeId}"`,
        );
      }

      const disc: DiscSelection = { file, label, volumeId: volume.volumeId, sourceZip };
      if (expected === 'GDI') onGdi(disc);
      else onNod(disc);
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      if (expected === 'GDI') gdiError = msg;
      else nodError = msg;
    } finally {
      busy = false;
    }
  }

  function discSummary(disc: DiscSelection): string {
    if (disc.sourceZip) {
      return `${disc.sourceZip} → ${disc.file.name}`;
    }
    return disc.file.name;
  }

  async function handleReleaseZip(input: HTMLInputElement) {
    const file = input.files?.[0];
    if (!file) return;

    busy = true;
    releaseError = '';

    try {
      const { extractReleaseAssets, describeReleaseAssets } = await import('../lib/release-zip');
      const assets = await extractReleaseAssets(file);
      onRelease({
        file,
        assetNames: describeReleaseAssets(assets),
      });
    } catch (err) {
      releaseError = err instanceof Error ? err.message : String(err);
      onRelease(null);
    } finally {
      busy = false;
    }
  }

  function clearRelease() {
    releaseError = '';
    onRelease(null);
  }

  const canContinue = $derived(Boolean(gdi && nod));
</script>

<section class="space-y-6">
  <div>
    <h2 class="cnc-step-title">1. Choose install discs</h2>
    <p class="mt-2 text-sm text-stone-400">
      Select both GDI and NOD install media from your own copy of Command &amp; Conquer for the
      Atari ST. Processing stays in your browser — nothing is uploaded.
    </p>
  </div>

  <div class="cnc-field space-y-3">
    <p class="text-sm font-medium">GDI install disc (required)</p>
    <p class="text-xs text-stone-500">ISO, BIN, IMG, or ZIP with a single disc image inside.</p>
    <input
      id="gdi-disc"
      type="file"
      accept={DISC_PICKER_ACCEPT}
      disabled={busy}
      onchange={(e) => handleDisc(e.currentTarget, 'GDI')}
      class="block w-full text-sm file:mr-3 file:rounded file:border-0 file:bg-lime-700 file:px-3 file:py-1.5 file:text-stone-950"
    />
    {#if gdi}
      <p class="text-sm text-cnc-gold">
        {discSummary(gdi)} ({gdi.volumeId})
      </p>
    {/if}
    {#if gdiError}
      <p class="text-sm text-red-400">{gdiError}</p>
    {/if}
  </div>

  <div class="cnc-field space-y-3">
    <p class="text-sm font-medium">NOD install disc (required)</p>
    <p class="text-xs text-stone-500">ISO, BIN, IMG, or ZIP with a single disc image inside.</p>
    <input
      id="nod-disc"
      type="file"
      accept={DISC_PICKER_ACCEPT}
      disabled={busy}
      onchange={(e) => handleDisc(e.currentTarget, 'NOD')}
      class="block w-full text-sm file:mr-3 file:rounded file:border-0 file:bg-stone-600 file:px-3 file:py-1.5 file:text-stone-100"
    />
    {#if nod}
      <p class="text-sm text-stone-300">
        {discSummary(nod)} ({nod.volumeId})
      </p>
    {/if}
    {#if nodError}
      <p class="text-sm text-red-400">{nodError}</p>
    {/if}
  </div>

  <div class="cnc-field space-y-3">
    <p class="text-sm font-medium">C&amp;C Atari ST release ZIP (optional)</p>
    <p class="text-xs text-stone-500">
      Download from
      <a
        class="cnc-link"
        href="https://indyjo.itch.io/commandconquer"
        target="_blank"
        rel="noreferrer">itch.io</a
      >
      — adds <code class="text-stone-400">cnc.tos</code> and
      <code class="text-stone-400">*.w16</code> palette files to your output ZIP.
    </p>
    <input
      id="release-zip"
      type="file"
      accept=".zip,application/zip"
      disabled={busy}
      onchange={(e) => handleReleaseZip(e.currentTarget)}
      class="block w-full text-sm file:mr-3 file:rounded file:border-0 file:bg-stone-600 file:px-3 file:py-1.5 file:text-stone-100"
    />
    {#if release}
      <p class="text-sm text-stone-300">
        {release.file.name} — {release.assetNames.length} file(s)
        (cnc.tos + {release.assetNames.filter((n) => n.toLowerCase().endsWith('.w16')).length}×
        .w16)
      </p>
      <ul class="max-h-24 overflow-y-auto text-xs font-mono text-stone-500 list-disc pl-5">
        {#each release.assetNames as name}
          <li>{name}</li>
        {/each}
      </ul>
      <button
        type="button"
        onclick={clearRelease}
        class="text-xs text-stone-500 underline hover:text-stone-300"
      >
        Remove release ZIP
      </button>
    {/if}
    {#if releaseError}
      <p class="text-sm text-red-400">{releaseError}</p>
    {/if}
  </div>

  <p class="text-xs text-stone-500">
    You must legally own Command &amp; Conquer. This tool does not distribute Electronic Arts assets.
  </p>

  <div class="flex justify-end">
    <button
      type="button"
      disabled={!canContinue}
      onclick={onNext}
      class="cnc-btn-primary disabled:opacity-40"
    >
      Continue
    </button>
  </div>
</section>
