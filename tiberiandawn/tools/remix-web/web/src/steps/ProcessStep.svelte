<script lang="ts">
  import { runPipeline } from '../lib/pipeline';
  import type { ContentOptions, DiscSelection, CheckoutPayload, ProcessLogLine, ProcessProgress, ReleaseSelection, TargetVersionState } from '../lib/types';
  import ProcessLog from './ProcessLog.svelte';

  interface Props {
    gdi: DiscSelection;
    nod: DiscSelection;
    release: ReleaseSelection | null;
    contentOptions: ContentOptions;
    targetVersion: TargetVersionState;
    onBack: () => void;
    onContinue: (payload: CheckoutPayload) => void;
  }

  let { gdi, nod, release, contentOptions, targetVersion, onBack, onContinue }: Props = $props();

  let progress = $state<ProcessProgress>({
    phase: 'idle',
    done: 0,
    total: 0,
    log: [],
  });
  let started = $state(false);
  let finished = $state(false);
  let failed = $state('');
  let resultBlob = $state<Blob | null>(null);
  let resultFiles = $state<Map<string, Uint8Array> | null>(null);
  let resultNames = $state<string[]>([]);

  async function start() {
    if (started) return;
    started = true;
    finished = false;
    failed = '';
    resultBlob = null;
    resultFiles = null;
    resultNames = [];
    progress = {
      phase: 'scan',
      done: 0,
      total: 0,
      log: [{ level: 'info', text: 'Starting…' }],
    };

    try {
      const result = await runPipeline(
        {
          gdi,
          nod,
          release,
          contentOptions,
          targetVersion: targetVersion.version,
          wasmBaseUrl: import.meta.env.BASE_URL,
        },
        (p) => {
          progress = p;
        },
      );
      resultBlob = result.zipBlob;
      resultFiles = result.files;
      resultNames = result.fileNames;
      finished = true;
    } catch (err) {
      failed = err instanceof Error ? err.message : String(err);
      progress = {
        ...progress,
        phase: 'error',
        log: [...progress.log, { level: 'error', text: failed }],
      };
    }
  }

  function continueToCheckout() {
    if (!resultBlob || !resultFiles) return;
    onContinue({
      zipBlob: resultBlob,
      files: resultFiles,
      fileNames: resultNames,
      log: progress.log,
    });
  }

  const pct = $derived(
    progress.total > 0 ? Math.round((progress.done / progress.total) * 100) : 0,
  );

  function encodeVerb(phase: string): string {
    if (phase === 'start') return 'Starting';
    if (phase === 'prep') return 'Preparing';
    return 'Encoding';
  }

  function encodeJobLabel(enc: { phase: string; label: string; done: number; total: number }): string {
    const verb = encodeVerb(enc.phase);
    if (enc.total <= 0) return `${verb} ${enc.label}…`;
    return `${verb} ${enc.label} — frame ${enc.done} / ${enc.total}`;
  }

  function encodeJobPct(enc: { done: number; total: number }): number {
    return enc.total > 0 ? Math.round((enc.done / enc.total) * 100) : 0;
  }

  const activeEncodes = $derived(progress.encodes ?? []);
</script>

<section class="space-y-6">
  <div>
    <h2 class="cnc-step-title">3. Process</h2>
    <p class="mt-2 text-sm text-stone-400">
      Extract MIX files from the ISO, then repack each one with REMIX (audio conversion, even
      offsets). Remix runs in a background worker so the page stays responsive — movie encoding can
      still take a long time.
    </p>
  </div>

  {#if !started}
    <button type="button" onclick={start} class="cnc-btn-primary">Start processing</button>
  {:else}
    <div class="cnc-panel space-y-2">
      <div class="flex justify-between text-sm">
        <span class="capitalize text-stone-300">{progress.phase}</span>
        {#if progress.current}
          <span class="font-mono text-cnc-gold">{progress.current}</span>
        {/if}
      </div>
      {#if progress.total > 0}
        <div class="cnc-progress-track">
          <div class="cnc-progress-bar" style="width: {pct}%"></div>
        </div>
        <p class="text-xs text-stone-500">{progress.done} / {progress.total} MIX files</p>
      {/if}
      {#if activeEncodes.length > 0}
        <div class="space-y-2 border-t border-stone-700/60 pt-2">
          <p class="text-xs text-stone-500">
            {activeEncodes.length} encode{activeEncodes.length === 1 ? '' : 's'} in flight
          </p>
          {#each activeEncodes as enc (enc.label)}
            <div class="space-y-1">
              <p class="font-mono text-xs text-cnc-gold">{encodeJobLabel(enc)}</p>
              {#if enc.total > 0}
                <div class="cnc-progress-track">
                  <div class="cnc-progress-bar" style="width: {encodeJobPct(enc)}%"></div>
                </div>
              {/if}
            </div>
          {/each}
        </div>
      {/if}
    </div>
  {/if}

  {#if finished}
    <p class="text-sm text-cnc-gold">
      Processing complete — review the log below, then continue to checkout.
    </p>
  {/if}

  {#if failed}
    <p class="text-sm text-red-400">{failed}</p>
  {/if}

  {#if started}
    <ProcessLog log={progress.log} maxHeightClass="max-h-96" />
  {/if}

  <div class="flex justify-between gap-3">
    <button
      type="button"
      disabled={started && !finished && progress.phase !== 'error'}
      onclick={onBack}
      class="cnc-btn-secondary disabled:opacity-40"
    >
      Back
    </button>
    {#if finished}
      <button type="button" onclick={continueToCheckout} class="cnc-btn-primary">
        Continue to checkout
      </button>
    {/if}
  </div>
</section>
