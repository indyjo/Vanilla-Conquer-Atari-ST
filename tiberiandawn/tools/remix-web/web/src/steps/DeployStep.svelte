<script lang="ts">
  import { downloadBlob } from '../lib/zip';
  import type { ProcessLogLine, ReleaseSelection } from '../lib/types';
  import ProcessLog from './ProcessLog.svelte';

  interface Props {
    zipBlob: Blob | null;
    files: Map<string, Uint8Array> | null;
    fileNames: string[];
    processLog: ProcessLogLine[];
    release: ReleaseSelection | null;
    onRestart: () => void;
  }

  let { zipBlob, files, fileNames, processLog, release, onRestart }: Props = $props();

  let showLog = $state(false);

  const outputNames = $derived(
    files && files.size > 0 ? [...files.keys()] : fileNames,
  );

  const mixCount = $derived(
    outputNames.filter((n) => typeof n === 'string' && n.toUpperCase().endsWith('.MIX')).length,
  );
  const releaseCount = $derived(outputNames.length - mixCount);

  function download() {
    if (!zipBlob) return;
    downloadBlob(zipBlob, release ? 'cncst-ready.zip' : 'cncst-mix.zip');
  }
</script>

<section class="space-y-6">
  <div>
    <h2 class="cnc-step-title">4. Checkout</h2>
    <p class="mt-2 text-sm text-stone-400">
      {#if release}
        Download a ready-to-copy ZIP with repacked MIX files, <code class="text-cnc-gold">cnc.tos</code
        >, and palette weights.
      {:else}
        Download the repacked MIX files and copy them next to <code class="text-cnc-gold">cnc.tos</code
        >
        on your Atari ST drive — or go back and add the itch.io release ZIP on step 1.
      {/if}
    </p>
  </div>

  {#if zipBlob}
    <div class="cnc-panel space-y-3">
      <p class="text-sm">
        Ready: <strong class="text-cnc-gold">{outputNames.length}</strong> files ({Math.round(
          zipBlob.size / 1024,
        )} KiB ZIP)
        {#if release}
          — {mixCount} MIX + {releaseCount} from release
        {/if}
      </p>
      <ul class="max-h-48 overflow-y-auto text-xs font-mono text-stone-400 list-disc pl-5 cnc-scroll">
        {#each outputNames as name}
          <li>{name}</li>
        {/each}
      </ul>
      <button type="button" onclick={download} class="cnc-btn-primary">Download ZIP</button>
    </div>
  {/if}

  {#if processLog.length > 0}
    <div class="space-y-2">
      <button
        type="button"
        onclick={() => (showLog = !showLog)}
        class="text-sm text-stone-400 underline hover:text-stone-200"
      >
        {showLog ? 'Hide' : 'Show'} process log ({processLog.length} lines)
      </button>
      {#if showLog}
        <ProcessLog log={processLog} maxHeightClass="max-h-72" />
      {/if}
    </div>
  {/if}

  {#if !release}
    <p class="text-xs text-stone-500">
      Tip: on step 1 you can attach the
      <a
        class="text-cnc-gold underline"
        href="https://indyjo.itch.io/commandconquer"
        target="_blank"
        rel="noreferrer">itch.io release ZIP</a
      >
      to bundle <code>cnc.tos</code> and <code>*.w16</code> files automatically.
    </p>
  {/if}

  <button type="button" onclick={onRestart} class="cnc-btn-secondary">Start over</button>
</section>
