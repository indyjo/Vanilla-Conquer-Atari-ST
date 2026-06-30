<script lang="ts">
  import AcquireStep from './steps/AcquireStep.svelte';
  import ContentStep from './steps/ContentStep.svelte';
  import ProcessStep from './steps/ProcessStep.svelte';
  import DeployStep from './steps/DeployStep.svelte';
  import { DEFAULT_CONTENT_OPTIONS } from './lib/content-options';
  import { defaultTargetVersionState } from './lib/target-version';
  import type {
    CheckoutPayload,
    ContentOptions,
    DiscSelection,
    ProcessLogLine,
    ReleaseSelection,
    TargetVersionState,
    WizardStep,
  } from './lib/types';

  let step = $state<WizardStep>('discs');
  let gdi = $state<DiscSelection | null>(null);
  let nod = $state<DiscSelection | null>(null);
  let release = $state<ReleaseSelection | null>(null);
  let targetVersion = $state<TargetVersionState>(defaultTargetVersionState());
  let contentOptions = $state<ContentOptions>({ ...DEFAULT_CONTENT_OPTIONS });
  let zipBlob = $state<Blob | null>(null);
  let outputFiles = $state<Map<string, Uint8Array> | null>(null);
  let fileNames = $state<string[]>([]);
  let processLog = $state<ProcessLogLine[]>([]);

  const steps: WizardStep[] = ['discs', 'customize', 'process', 'checkout'];

  const stepTabLabels: Record<WizardStep, string> = {
    discs: 'DISCS',
    customize: 'CUSTOMIZE',
    process: 'PROCESS',
    checkout: 'CHECKOUT',
  };

  function restart() {
    step = 'discs';
    gdi = null;
    nod = null;
    release = null;
    targetVersion = defaultTargetVersionState();
    contentOptions = { ...DEFAULT_CONTENT_OPTIONS };
    zipBlob = null;
    outputFiles = null;
    fileNames = [];
    processLog = [];
  }
</script>

<div class="mx-auto flex min-h-screen max-w-2xl flex-col px-4 py-8">
  <header class="mb-8 border-b border-cnc-bronze/30 pb-6">
    <p class="text-xs font-semibold uppercase tracking-[0.2em] text-cnc-gold/80">
      Command &amp; Conquer for the Atari ST
    </p>
    <h1 class="cnc-title mt-1">Remix Web</h1>
    <p class="mt-2 text-sm text-stone-400">
      Prepare game MIX files locally in your browser — nothing is uploaded.
    </p>
    <nav class="mt-4 flex flex-wrap gap-2 text-xs font-semibold tracking-wide">
      {#each steps as s, i}
        <span class="cnc-nav-pill {step === s ? 'cnc-nav-pill-active' : ''}">
          {i + 1}. {stepTabLabels[s]}
        </span>
      {/each}
    </nav>
  </header>

  <main class="flex-1">
    {#if step === 'discs'}
      <AcquireStep
        {gdi}
        {nod}
        {release}
        onGdi={(d) => (gdi = d)}
        onNod={(d) => (nod = d)}
        onRelease={(d) => {
          release = d;
        }}
        onTargetVersionFromRelease={(state) => {
          targetVersion = state;
          contentOptions = {
            ...contentOptions,
            convertSt16Iconsets: state.version === '0.2.x',
          };
        }}
        onNext={() => (step = 'customize')}
      />
    {:else if step === 'customize'}
      <ContentStep
        bind:options={contentOptions}
        bind:targetVersion
        onChange={(o) => (contentOptions = o)}
        onTargetVersionChange={(v) => (targetVersion = v)}
        onBack={() => (step = 'discs')}
        onNext={() => (step = 'process')}
      />
    {:else if step === 'process' && gdi && nod}
      <ProcessStep
        {gdi}
        {nod}
        {release}
        {contentOptions}
        {targetVersion}
        onBack={() => (step = 'customize')}
        onContinue={(payload: CheckoutPayload) => {
          zipBlob = payload.zipBlob;
          outputFiles = payload.files;
          fileNames = payload.fileNames;
          processLog = payload.log;
          step = 'checkout';
        }}
      />
    {:else if step === 'checkout'}
      <DeployStep {zipBlob} files={outputFiles} {fileNames} {processLog} {release} onRestart={restart} />
    {/if}
  </main>
</div>
