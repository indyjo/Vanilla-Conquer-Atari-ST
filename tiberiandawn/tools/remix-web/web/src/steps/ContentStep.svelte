<script lang="ts">
  import type {
    ContentOptions,
    TargetVersion,
    TargetVersionState,
    VideoEffort,
    VideoParallelism,
    VideoQuality,
  } from '../lib/types';
  import {
    defaultShpxForVersion,
    defaultSt16ForVersion,
    moviesSupportedForVersion,
    shpxIncompatibilityWarning,
    st16IncompatibilityWarning,
    TARGET_VERSION_OPTIONS,
    VIDEO_EFFORT_OPTIONS,
    VIDEO_PARALLELISM_OPTIONS,
    VIDEO_QUALITY_OPTIONS,
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

  const moviesOk = $derived(moviesSupportedForVersion(targetVersion.version));

  function toggle(key: keyof ContentOptions, value: boolean) {
    if (key === 'movieSequences' && !moviesOk) return;
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
    const movies = moviesSupportedForVersion(version);
    const nextOptions: ContentOptions = {
      ...options,
      ...(!st16Touched ? { convertSt16Iconsets: defaultSt16ForVersion(version) } : {}),
      ...(!shpxTouched ? { convertShpx: defaultShpxForVersion(version) } : {}),
      movieSequences: movies ? options.movieSequences : false,
    };
    options = nextOptions;
    onChange(nextOptions);
  }

  function setVideoQuality(value: VideoQuality) {
    const next = { ...options, videoQuality: value };
    options = next;
    onChange(next);
  }

  function setVideoEffort(value: VideoEffort) {
    const next = { ...options, videoEffort: value };
    options = next;
    onChange(next);
  }

  function setVideoParallelism(value: VideoParallelism) {
    const next = { ...options, videoParallelism: value };
    options = next;
    onChange(next);
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
          >CONQUER / TEMPERAT / DESERT / WINTER — external shape pools; saves RAM; mandatory on
          4&nbsp;MB Atari STs.</span
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

    <label class="cnc-field flex items-start gap-3" class:opacity-60={!moviesOk}>
      <input
        type="checkbox"
        checked={options.movieSequences}
        disabled={!moviesOk}
        onchange={(e) => toggle('movieSequences', e.currentTarget.checked)}
        class="mt-1"
      />
      <span>
        <span class="font-medium">Movie sequences</span>
        <span class="block text-xs text-stone-400">
          {#if moviesOk}
            MOVIES.MIX — merge GDI+NOD, then encode VQAs in parallel to STVQ (needs video/*.w16)
          {:else}
            Requires target version 0.3.x
          {/if}
        </span>
      </span>
    </label>

    {#if moviesOk && options.movieSequences}
      <div class="ml-7 space-y-3 rounded border border-stone-700/60 bg-stone-900/40 p-3">
        <label class="block text-sm">
          <span class="font-medium text-stone-200">Video quality</span>
          <select
            class="cnc-select mt-1 block max-w-md text-sm"
            value={options.videoQuality}
            onchange={(e) => setVideoQuality(e.currentTarget.value as VideoQuality)}
          >
            {#each VIDEO_QUALITY_OPTIONS as opt (opt.value)}
              <option value={opt.value}>{opt.label}</option>
            {/each}
          </select>
        </label>
        <label class="block text-sm">
          <span class="font-medium text-stone-200">Encoding effort</span>
          <select
            class="cnc-select mt-1 block max-w-md text-sm"
            value={options.videoEffort}
            onchange={(e) => setVideoEffort(e.currentTarget.value as VideoEffort)}
          >
            {#each VIDEO_EFFORT_OPTIONS as opt (opt.value)}
              <option value={opt.value}>{opt.label}</option>
            {/each}
          </select>
        </label>
        <label class="block text-sm">
          <span class="font-medium text-stone-200">Parallel encodes</span>
          <select
            class="cnc-select mt-1 block max-w-md text-sm"
            value={options.videoParallelism}
            onchange={(e) =>
              setVideoParallelism(Number(e.currentTarget.value) as VideoParallelism)}
          >
            {#each VIDEO_PARALLELISM_OPTIONS as opt (opt.value)}
              <option value={opt.value}>{opt.label}</option>
            {/each}
          </select>
          <span class="mt-1 block text-xs text-stone-500">
            More workers use more RAM (each loads its own WASM). 4 is a good default.
          </span>
        </label>
        <p class="text-xs text-stone-500">
          Encoding every clip can take a long time in the browser. Prefer a machine that can stay
          awake until processing finishes.
        </p>
      </div>
    {/if}
  </div>

  <div class="flex justify-between">
    <button type="button" onclick={onBack} class="cnc-btn-secondary">Back</button>
    <button type="button" onclick={onNext} class="cnc-btn-primary">Continue</button>
  </div>
</section>
