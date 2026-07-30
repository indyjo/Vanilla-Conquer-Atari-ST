<script lang="ts">
  import type { ProcessLogLine } from '../lib/types';

  interface Props {
    log: ProcessLogLine[];
    maxHeightClass?: string;
  }

  let { log, maxHeightClass = 'max-h-72' }: Props = $props();

  let logEl = $state<HTMLDivElement | null>(null);

  $effect(() => {
    log.length;
    if (logEl) {
      logEl.scrollTop = logEl.scrollHeight;
    }
  });

  const errorCount = $derived(log.filter((l) => l.level === 'error').length);
  const warnCount = $derived(log.filter((l) => l.level === 'warn').length);

  function copyLog() {
    const text = log.map((l) => `[${l.level}] ${l.text}`).join('\n');
    void navigator.clipboard.writeText(text);
  }
</script>

<div class="space-y-2">
  {#if log.length > 0}
    <div class="flex items-center justify-between gap-2 text-xs text-[#8a8a8a]">
      <span>
        {log.length} line(s)
        {#if warnCount > 0}
          · <span class="text-[#fab05b]">{warnCount} warning(s)</span>
        {/if}
        {#if errorCount > 0}
          · <span class="text-red-400">{errorCount} error(s)</span>
        {/if}
      </span>
      <button
        type="button"
        onclick={copyLog}
        class="rounded border border-[#4b4b4b] px-2 py-0.5 text-[#b0b0b0] hover:text-[#e8e8e8]"
      >
        Copy log
      </button>
    </div>
  {/if}

  <div
    bind:this={logEl}
    class="overflow-y-auto rounded border border-[#4b4b4b] bg-[#1a1a1a] p-3 font-mono text-xs {maxHeightClass}"
  >
    {#each log as line}
      <p
        class={line.level === 'error'
          ? 'text-red-400'
          : line.level === 'warn'
            ? 'text-[#fab05b]'
            : 'text-[#b0b0b0]'}
      >
        {line.text}
      </p>
    {/each}
  </div>
</div>
