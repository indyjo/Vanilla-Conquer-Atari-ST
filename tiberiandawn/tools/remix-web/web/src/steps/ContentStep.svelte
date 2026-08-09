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

  const is03 = $derived(targetVersion.version === '0.3.x');
  const moviesOk = $derived(moviesSupportedForVersion(targetVersion.version));

  function toggle(key: keyof ContentOptions, value: boolean) {
    if (key === 'movieSequences' && !moviesOk) return;
    if (key === 'convertSt16Iconsets') st16Touched = true;
    if (key === 'convertShpx') shpxTouched = true;
    let next = { ...options, [key]: value };
    /* 0.3.x: music is nested under Audio — clearing Audio drops Include music. */
    if (key === 'speechAndSfx' && !value && targetVersion.version === '0.3.x') {
      next = { ...next, musicScores: false };
    }
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
      movieSequences: movies ? (version === '0.3.x' ? true : options.movieSequences) : false,
      ...(version === '0.3.x'
        ? { convertSt16Iconsets: true, convertShpx: true, speechAndSfx: true, musicScores: true }
        : {}),
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
    <p class="mt-2 text-sm text-[#b0b0b0]">
      Choose optional MIX archives and remix options. Core game data is always extracted when
      present.
    </p>
  </div>

  <div class="cnc-panel space-y-3">
    <p class="text-sm font-medium text-[#e8e8e8]">Target C&C4ST version</p>
    <p class="text-xs text-[#8a8a8a]">
      {#if targetVersion.source === 'release'}
        Pre-filled from the release ZIP filename when possible.
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
    {#if !is03}
      <label class="cnc-field flex items-start gap-3">
        <input
          type="checkbox"
          checked={options.convertSt16Iconsets}
          onchange={(e) => toggle('convertSt16Iconsets', e.currentTarget.checked)}
          class="mt-1"
        />
        <span>
          <span class="font-medium">Convert terrain iconsets to ST16</span>
          <span class="block text-xs text-[#b0b0b0]"
            >TEMPERAT, DESERT, WINTER MIX — requires matching *.W16 from release ZIP</span
          >
        </span>
      </label>

      {#if st16Warning}
        <p class="rounded border border-[#fab05b]/40 bg-[#322312]/50 px-3 py-2 text-xs text-[#fab05b]">
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
          <span class="block text-xs text-[#b0b0b0]"
            >CONQUER / TEMPERAT / DESERT / WINTER — external shape pools; saves RAM.</span
          >
        </span>
      </label>

      {#if shpxWarning}
        <p class="rounded border border-[#fab05b]/40 bg-[#322312]/50 px-3 py-2 text-xs text-[#fab05b]">
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
          <span class="block text-xs text-[#b0b0b0]">SOUNDS.MIX, SPEECH.MIX</span>
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
          <span class="block text-xs text-[#b0b0b0]">SCORES.MIX (~large)</span>
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
          <span class="block text-xs text-[#b0b0b0]">
            {#if moviesOk}
              MOVIES.MIX — encode VQAs to STVQ (needs video/*.w16)
            {:else}
              Requires target version 0.3.x
            {/if}
          </span>
        </span>
      </label>
    {:else}
      <label class="cnc-field flex items-start gap-3">
        <input
          type="checkbox"
          checked={options.speechAndSfx}
          onchange={(e) => toggle('speechAndSfx', e.currentTarget.checked)}
          class="mt-1"
        />
        <span>
          <span class="font-medium">Audio</span>
          <span class="block text-xs text-[#b0b0b0]"
            >Speech, SFX, and transit AUDs → AUDX in SOUNDS/SPEECH (optional music → SCORES).</span
          >
        </span>
      </label>

      {#if options.speechAndSfx}
        <label class="cnc-field ml-7 flex items-start gap-3">
          <input
            type="checkbox"
            checked={options.musicScores}
            onchange={(e) => toggle('musicScores', e.currentTarget.checked)}
            class="mt-1"
          />
          <span>
            <span class="font-medium">Include music</span>
            <span class="block text-xs text-[#b0b0b0]"
              >Uncheck to omit music and save disk space.</span
            >
          </span>
        </label>
      {/if}

      <label class="cnc-field flex items-start gap-3">
        <input
          type="checkbox"
          checked={options.movieSequences}
          onchange={(e) => toggle('movieSequences', e.currentTarget.checked)}
          class="mt-1"
        />
        <span>
          <span class="font-medium">Video</span>
          <span class="block text-xs text-[#b0b0b0]"
            >Uncheck to save disk space and encoding time.</span
          >
        </span>
      </label>
    {/if}

    {#if moviesOk && options.movieSequences}
      <div class="ml-7 space-y-3 rounded border border-[#4b4b4b] bg-[#1f1f1f] p-3">
        <label class="block text-sm">
          <span class="font-medium text-[#e8e8e8]">Video quality</span>
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
          <span class="font-medium text-[#e8e8e8]">Encoding effort</span>
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
          <span class="font-medium text-[#e8e8e8]">Parallel encodes</span>
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
        </label>
      </div>
    {/if}
  </div>

  <div class="flex justify-between gap-3 pt-2">
    <button type="button" class="cnc-btn-secondary" onclick={onBack}>Back</button>
    <button type="button" class="cnc-btn-primary" onclick={onNext}>Next</button>
  </div>
</section>
