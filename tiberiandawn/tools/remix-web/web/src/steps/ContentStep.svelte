<script lang="ts">
  import type { ContentOptions } from '../lib/types';

  interface Props {
    options: ContentOptions;
    onChange: (options: ContentOptions) => void;
    onBack: () => void;
    onNext: () => void;
  }

  let { options = $bindable(), onChange, onBack, onNext }: Props = $props();

  function toggle(key: keyof ContentOptions, value: boolean) {
    if (key === 'movieSequences') return;
    const next = { ...options, [key]: value };
    options = next;
    onChange(next);
  }
</script>

<section class="space-y-6">
  <div>
    <h2 class="cnc-step-title">2. Customize content</h2>
    <p class="mt-2 text-sm text-stone-400">
      Choose optional MIX archives to include. Core game data is always extracted when present.
    </p>
  </div>

  <div class="space-y-3">
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

    <label
      class="cnc-field flex items-start gap-3 opacity-60"
    >
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
