<script lang="ts">
  import type { ContentOptions, TargetVersion, TargetVersionState } from '../lib/types';
  import {
    defaultShpxForVersion,
    defaultSt16ForVersion,
    shpxIncompatibilityWarning,
    st16IncompatibilityWarning,
    TARGET_VERSION_OPTIONS,
  } from '../lib/target-version';

  interface Props {
    options: ContentOptions;
    targetVersion: TargetVersionState;
    onChange: (options: ContentOptions) => void;
    onTargetVersionChange: (state: TargetVersionState) => void;
    onBack: () => void;
    onNext: () => void;
  }

  let {
    options = $bindable(),
    targetVersion = $bindable(),
    onChange,
    onTargetVersionChange,
    onBack,
    onNext,
  }: Props = $props();

  let st16Touched = $state(false);
  let shpxTouched = $state(false);

  function toggle(key: keyof ContentOptions, value: boolean) {
    if (key === 'movieSequences') return;
    if (key === 'convertSt16Iconsets') st16Touched = true;
    if (key === 'convertShpx') shpxTouched = true;
    const next = { ...options, [key]: value };
    options = next;
    onChange(next);
  }

  function setTargetVersion(version: TargetVersion) {
    const next: TargetVersionState = { version, source: 'manual' };
    targetVersion = next;
    onTargetVersionChange(next);
    if (!st16Touched || !shpxTouched) {
      const nextOptions = {
        ...options,
        ...(!st16Touched ? { convertSt16Iconsets: defaultSt16ForVersion(version) } : {}),
        ...(!shpxTouched ? { convertShpx: defaultShpxForVersion(version) } : {}),
      };
      options = nextOptions;
      onChange(nextOptions);
    }
  }

  const st16Warning = $derived(
    st16IncompatibilityWarning(targetVersion.version, options.convertSt16Iconsets),
  );
  const shpxWarning = $derived(
    shpxIncompatibilityWarning(targetVersion.version, options.convertShpx),
  );
</script>

<section class="space-y-6">
  <div>
    <h2 class="cnc-step-title">2. Customize content</h2>
    <p class="mt-2 text-sm text-stone-400">
      Choose optional MIX archives and remix options. Core game data is always extracted when
      present.
    </p>
  </div>

  <div class="cnc-panel space-y-3">
    <p class="text-sm font-medium text-stone-200">Target C&C4ST version</p>
    <p class="text-xs text-stone-500">
      {#if targetVersion.source === 'release'}
        Pre-filled from release ZIP readme when available.
      {:else}
        Choose the port version you plan to run.
      {/if}
    </p>
    <select
      class="cnc-select mt-1 max-w-md text-sm"
      value={targetVersion.version}
      onchange={(e) => setTargetVersion(e.currentTarget.value as TargetVersion)}
      aria-label="Target C&C4ST version"
    >
      {#each TARGET_VERSION_OPTIONS as opt (opt.value)}
        <option value={opt.value}>{opt.label}</option>
      {/each}
    </select>
  </div>

  <div class="space-y-3">
    <label class="cnc-field flex items-start gap-3">
      <input
        type="checkbox"
        checked={options.convertSt16Iconsets}
        onchange={(e) => toggle('convertSt16Iconsets', e.currentTarget.checked)}
        class="mt-1"
      />
      <span>
        <span class="font-medium">Convert terrain iconsets to ST16</span>
        <span class="block text-xs text-stone-400"
          >TEMPERAT, DESERT, WINTER MIX — requires matching *.W16 from release ZIP</span
        >
      </span>
    </label>

    {#if st16Warning}
      <p class="rounded border border-amber-700/50 bg-amber-950/40 px-3 py-2 text-xs text-amber-200">
        {st16Warning}
      </p>
    {/if}

    <label class="cnc-field flex items-start gap-3">
      <input
        type="checkbox"
        checked={options.convertShpx}
        onchange={(e) => toggle('convertShpx', e.currentTarget.checked)}
        class="mt-1"
      />
      <span>
        <span class="font-medium">Convert shapes to SHPX</span>
        <span class="block text-xs text-stone-400"
          >CONQUER.MIX — external shape pool; saves RAM; mandatory on 4&nbsp;MB Atari STs.</span
        >
      </span>
    </label>

    {#if shpxWarning}
      <p class="rounded border border-amber-700/50 bg-amber-950/40 px-3 py-2 text-xs text-amber-200">
        {shpxWarning}
      </p>
    {/if}

    <label class="cnc-field flex items-start gap-3">
      <input
        type="checkbox"
        checked={options.speechAndSfx}
        onchange={(e) => toggle('speechAndSfx', e.currentTarget.checked)}
        class="mt-1"
      />
      <span>
        <span class="font-medium">Speech and sound effects</span>
        <span class="block text-xs text-stone-400">AUD.MIX, SOUNDS.MIX, SPEECH.MIX</span>
      </span>
    </label>

    <label class="cnc-field flex items-start gap-3">
      <input
        type="checkbox"
        checked={options.musicScores}
        onchange={(e) => toggle('musicScores', e.currentTarget.checked)}
        class="mt-1"
      />
      <span>
        <span class="font-medium">Music</span>
        <span class="block text-xs text-stone-400">SCORES.MIX (~large)</span>
      </span>
    </label>

    <label class="cnc-field flex items-start gap-3 opacity-60">
      <input type="checkbox" checked={false} disabled class="mt-1" />
      <span>
        <span class="font-medium">Movie sequences</span>
        <span class="block text-xs text-stone-500"
          >MOVIES.MIX — not used by the Atari ST port (disabled)</span
        >
      </span>
    </label>
  </div>

  <div class="flex justify-between">
    <button type="button" onclick={onBack} class="cnc-btn-secondary">Back</button>
    <button type="button" onclick={onNext} class="cnc-btn-primary">Continue</button>
  </div>
</section>
