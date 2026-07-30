<script lang="ts">
  import type { DiscLabel, DiscSelection, ReleaseSelection, TargetVersionState } from '../lib/types';
  import { DISC_PICKER_ACCEPT, resolveDiscFile } from '../lib/disc-zip';

  interface Props {
    gdi: DiscSelection | null;
    nod: DiscSelection | null;
    release: ReleaseSelection | null;
    onGdi: (disc: DiscSelection | null) => void;
    onNod: (disc: DiscSelection | null) => void;
    onRelease: (release: ReleaseSelection | null) => void;
    onTargetVersionFromRelease: (state: TargetVersionState) => void;
    onNext: () => void;
  }

  let { gdi, nod, release, onGdi, onNod, onRelease, onTargetVersionFromRelease, onNext }: Props =
    $props();

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
      const { guessTargetVersionFromFilename } = await import('../lib/target-version');
      const assets = await extractReleaseAssets(file);
      onRelease({
        file,
        assetNames: describeReleaseAssets(assets),
      });
      onTargetVersionFromRelease({
        version: guessTargetVersionFromFilename(file.name),
        source: 'release',
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

  const canContinue = $derived(Boolean(gdi && nod && release));
</script>

<section class="space-y-6">
  <div>
    <h2 class="cnc-step-title">1. Choose install discs</h2>
    <p class="mt-2 text-sm text-[#b0b0b0]">
      Select both GDI and NOD install media from your own copy of Command &amp; Conquer, plus the
      Atari ST release ZIP. Processing stays in your browser — nothing is uploaded.
    </p>
  </div>

  <div class="cnc-field space-y-3">
    <p class="text-sm font-medium">GDI install disc</p>
    <p class="text-xs text-[#8a8a8a]">ISO, BIN, IMG, or ZIP with a single disc image inside.</p>
    <input
      id="gdi-disc"
      type="file"
      accept={DISC_PICKER_ACCEPT}
      class="cnc-file"
      disabled={busy}
      onchange={(e) => handleDisc(e.currentTarget, 'GDI')}
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
    <p class="text-sm font-medium">NOD install disc</p>
    <p class="text-xs text-[#8a8a8a]">ISO, BIN, IMG, or ZIP with a single disc image inside.</p>
    <input
      id="nod-disc"
      type="file"
      accept={DISC_PICKER_ACCEPT}
      class="cnc-file"
      disabled={busy}
      onchange={(e) => handleDisc(e.currentTarget, 'NOD')}
    />
    {#if nod}
      <p class="text-sm text-[#c8c8c8]">
        {discSummary(nod)} ({nod.volumeId})
      </p>
    {/if}
    {#if nodError}
      <p class="text-sm text-red-400">{nodError}</p>
    {/if}
  </div>

  <div class="cnc-field space-y-3">
    <p class="text-sm font-medium">C&amp;C Atari ST release ZIP</p>
    <p class="text-xs text-[#8a8a8a]">
      Download from
      <a
        class="cnc-link"
        href="https://indyjo.itch.io/commandconquer"
        target="_blank"
        rel="noreferrer">itch.io</a
      >
      — includes <code>cnc.tos</code> and
      <code>*.w16</code> weights for ST16 terrain conversion.
    </p>
    <input
      id="release-zip"
      type="file"
      accept=".zip,application/zip"
      class="cnc-file"
      disabled={busy}
      onchange={(e) => handleReleaseZip(e.currentTarget)}
    />
    {#if release}
      <p class="text-sm text-[#c8c8c8]">
        {release.file.name} — {release.assetNames.length} file(s)
        (cnc.tos + {release.assetNames.filter((n) => n.toLowerCase().endsWith('.w16') && !n.startsWith('video/')).length}×
        .w16{#if release.assetNames.some((n) => n.startsWith('video/'))}, {release.assetNames.filter((n) => n.startsWith('video/')).length}× video/*.w16{/if})
      </p>
      <button
        type="button"
        onclick={clearRelease}
        class="text-xs text-[#8a8a8a] underline hover:text-[#c8c8c8]"
      >
        Remove release ZIP
      </button>
    {/if}
    {#if releaseError}
      <p class="text-sm text-red-400">{releaseError}</p>
    {/if}
  </div>

  <p class="text-xs text-[#8a8a8a]">
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
