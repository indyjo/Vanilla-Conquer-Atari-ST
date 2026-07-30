<script lang="ts">
  import { downloadBlob } from '../lib/zip';
  import type { ProcessLogLine, ReleaseSelection } from '../lib/types';
  import ProcessLog from './ProcessLog.svelte';

  interface Props {
    zipBlob: Blob | null;
    files: Map<string, Uint8Array> | null;
    fileNames: string[];
    processLog: ProcessLogLine[];
    release: ReleaseSelection;
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
    downloadBlob(zipBlob, 'cnc.zip');
  }
</script>

<section class="space-y-6">
  <div>
    <h2 class="cnc-step-title">4. Checkout</h2>
    <p class="mt-2 text-sm text-[#b0b0b0]">
      Download a ready-to-copy ZIP with repacked MIX files and
      <code class="text-cnc-gold">cnc.tos</code> from {release.file.name}.
    </p>
  </div>

  {#if zipBlob}
    <div class="cnc-panel space-y-3">
      <p class="text-sm">
        Ready: <strong class="text-cnc-gold">{outputNames.length}</strong> files ({Math.round(
          zipBlob.size / 1024,
        )} KiB ZIP) — {mixCount} MIX + {releaseCount} from release
      </p>
      <button type="button" onclick={download} class="cnc-btn-primary">Download ZIP</button>
    </div>
  {/if}

  {#if processLog.length > 0}
    <div class="space-y-2">
      <button
        type="button"
        onclick={() => (showLog = !showLog)}
        class="text-sm text-[#b0b0b0] underline hover:text-[#e8e8e8]"
      >
        {showLog ? 'Hide' : 'Show'} process log ({processLog.length} lines)
      </button>
      {#if showLog}
        <ProcessLog log={processLog} maxHeightClass="max-h-72" />
      {/if}
    </div>
  {/if}

  <button type="button" onclick={onRestart} class="cnc-btn-secondary">Start over</button>
</section>
