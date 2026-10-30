<script setup lang="ts">
import { computed } from 'vue'

import Banner from '../../components/Banner.vue'
import Group from '../../components/Group.vue'
import Toggle from '../../components/Toggle.vue'
import FooterActionBar from '../../layout/FooterActionBar.vue'
import type { ExecutionProviderInfo } from '../../services/contract'
import { useNavigation } from '../../services/useNavigation'
import { useRuntime } from '../../services/useRuntime'

const { state, draft, dirty, saving, apply, revert, setRealEsrganPath, browseForRealEsrganPath } = useRuntime()
const { navigate } = useNavigation()

// Without the ONNX Runtime none of these settings can take effect, and with the
// AI layer switched off the provider/optimization choices do not apply yet —
// the old Settings page disabled them for exactly this reason.
const runtimeAvailable = computed(() => state.value?.diagnostic.runtimeInstalled ?? true)
const inferenceControlsEnabled = computed(() => runtimeAvailable.value && (draft.value?.enabled ?? false))

const fallbackActive = computed(() => {
  const diag = state.value?.diagnostic
  return Boolean(diag && diag.selectedProvider !== diag.activeProvider && diag.activeProvider !== '—')
})

function badgeFor(info: ExecutionProviderInfo): { kind: string; text: string } | null {
  if (info.status === 'Active') {
    return { kind: 'info', text: 'Active now' }
  }
  if (info.status === 'Available') {
    return { kind: 'ok', text: 'Available' }
  }
  if (info.status.startsWith('Not usable')) {
    return { kind: 'warn', text: 'Unavailable' }
  }
  return null
}

function disabledFor(info: ExecutionProviderInfo): boolean {
  return !info.supportedByPlatform || !info.compiledInRuntime
}
</script>

<template>
  <div class="body">
    <Banner v-if="!runtimeAvailable" severity="warn" title="ONNX Runtime is not available in this build">
      AI inference features are disabled. These settings are kept but have no effect until a build with the
      runtime is installed.
    </Banner>

    <Banner v-if="fallbackActive && state" severity="warn" title="Active provider differs from your selection">
      Selected: <span class="strong">{{ state.diagnostic.selectedProvider }}</span> ·
      Active: <span class="strong">{{ state.diagnostic.activeProvider }}</span>.
      <template v-if="state.diagnostic.lastError"> {{ state.diagnostic.lastError }}</template>
      <template #action>
        <a class="act" href="#" @click.prevent="navigate('system', 'diagnostics')">View Diagnostics</a>
      </template>
    </Banner>

    <Group v-if="draft" title="General">
        <div class="row" style="align-items: center">
          <span class="lbl" style="padding: 0">Enable Local AI Features</span>
          <Toggle v-model="draft.enabled" label="Enable Local AI Features" :disabled="!runtimeAvailable">
            <span class="muted">Turns the local ONNX layer on for selection, matting, background removal and upscale.</span>
          </Toggle>
        </div>
        <div class="sep" />
        <div class="row">
          <span class="lbl">Execution Provider</span>
          <div style="display: flex; flex-direction: column; gap: 6px; max-width: 640px" role="radiogroup" aria-label="Execution provider">
            <label class="radio-card" :class="{ on: draft.executionProvider === 'auto', off: !inferenceControlsEnabled }">
              <input
                type="radio"
                name="ep"
                :disabled="!inferenceControlsEnabled"
                :checked="draft.executionProvider === 'auto'"
                @change="draft.executionProvider = 'auto'"
              />
              <div>
                <div class="rc-t">Auto</div>
                <div class="rc-s">Uses the best compatible provider on this machine.</div>
              </div>
            </label>
            <label
              v-for="info in state?.availableProviders ?? []"
              :key="info.id"
              class="radio-card"
              :class="{ on: draft.executionProvider === info.id, off: !inferenceControlsEnabled || disabledFor(info) }"
            >
              <input
                type="radio"
                name="ep"
                :disabled="!inferenceControlsEnabled || disabledFor(info)"
                :checked="draft.executionProvider === info.id"
                @change="draft.executionProvider = info.id"
              />
              <div>
                <div class="rc-t">
                  {{ info.displayName }}
                  <span v-if="badgeFor(info)" class="bdg" :class="badgeFor(info)?.kind">
                    <span class="dot" />{{ badgeFor(info)?.text }}
                  </span>
                  <span v-else class="bdg">{{ info.status }}</span>
                </div>
                <div class="rc-s">{{ info.errorMessage || info.displayName }}</div>
              </div>
            </label>
          </div>
        </div>
        <div class="sep" />
        <div class="row" style="align-items: center">
          <span class="lbl" style="padding: 0">Allow CPU Fallback</span>
          <Toggle v-model="draft.allowCpuFallback" label="Allow CPU Fallback" :disabled="!inferenceControlsEnabled">
            <span class="muted">If the selected provider fails, run on CPU instead of stopping the job.</span>
          </Toggle>
        </div>
        <div class="row" style="align-items: center">
          <span class="lbl" style="padding: 0">Preload Models on Startup</span>
          <Toggle v-model="draft.preloadModelsOnStartup" label="Preload Models on Startup" :disabled="!inferenceControlsEnabled">
            <span class="muted">Loads default models when Hazor Studio starts. Faster first use, slower startup. Applies the next time Hazor Studio starts.</span>
          </Toggle>
        </div>
    </Group>

    <Group v-if="draft" title="Execution Optimizations">
        <template #meta>Advanced</template>
        <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 14px 24px">
          <Toggle v-model="draft.useFp16WhenAvailable" label="Use FP16 When Available" :disabled="!inferenceControlsEnabled" style="align-items: flex-start">
            <div><div class="strong" style="font-weight: 500">Use FP16 When Available</div><div class="help" style="margin-top: 2px">Half-precision inference on supported GPUs. Less memory, faster.</div></div>
          </Toggle>
          <Toggle v-model="draft.enableGraphOptimization" label="Enable ONNX Graph Optimizations" :disabled="!inferenceControlsEnabled" style="align-items: flex-start">
            <div><div class="strong" style="font-weight: 500">Enable ONNX Graph Optimizations</div><div class="help" style="margin-top: 2px">Fuses and simplifies operations when a model loads.</div></div>
          </Toggle>
          <Toggle v-model="draft.enableMemoryArena" label="Enable Memory Arena" :disabled="!inferenceControlsEnabled" style="align-items: flex-start">
            <div><div class="strong" style="font-weight: 500">Enable Memory Arena</div><div class="help" style="margin-top: 2px">Reuses allocated memory blocks between runs.</div></div>
          </Toggle>
          <Toggle v-model="draft.enableMemoryPattern" label="Enable Memory Pattern" :disabled="!inferenceControlsEnabled" style="align-items: flex-start">
            <div><div class="strong" style="font-weight: 500">Enable Memory Pattern</div><div class="help" style="margin-top: 2px">Pre-plans allocations for inputs with a fixed shape.</div></div>
          </Toggle>
        </div>
        <div v-if="!inferenceControlsEnabled" class="help" style="margin-top: 10px">
          {{ runtimeAvailable ? 'Enable Local AI Features to change these.' : 'Unavailable without the ONNX Runtime.' }}
        </div>
    </Group>

    <Group v-if="state" title="Upscale Backend">
        <div class="row">
          <span class="lbl">Real-ESRGAN Executable</span>
          <div>
            <div class="ctl" style="flex-wrap: nowrap">
              <input
                class="in mono"
                readonly
                aria-label="Real-ESRGAN executable"
                :value="state.realesrganExecutable || 'Automatic packaged backend'"
              />
              <button type="button" class="btn" @click="browseForRealEsrganPath()">Locate…</button>
              <button
                type="button"
                class="btn ghost"
                :disabled="!state.realesrganExecutable"
                @click="setRealEsrganPath('')"
              >
                Clear
              </button>
            </div>
            <div class="help">
              Applied immediately. Clear to go back to the backend packaged with Hazor Studio.
            </div>
          </div>
        </div>
    </Group>
  </div>

  <FooterActionBar :dirty="dirty" dirty-label="Unsaved changes" :saving="saving" @apply="apply" @revert="revert" />
</template>
