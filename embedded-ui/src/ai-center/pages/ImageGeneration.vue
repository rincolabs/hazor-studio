<script setup lang="ts">
import { ref } from 'vue'

import ComboField from '../components/ComboField.vue'
import Group from '../components/Group.vue'
import Icon from '../components/Icon.vue'
import NumberField from '../components/NumberField.vue'
import SegmentedControl from '../components/SegmentedControl.vue'
import SelectField from '../components/SelectField.vue'
import Toggle from '../components/Toggle.vue'
import Header from '../layout/Header.vue'
import PresetEditorShell from '../presets/PresetEditorShell.vue'
import type { ProviderType } from '../services/contract'
import { getClient } from '../services/client'
import { useNavigation } from '../services/useNavigation'
import { usePresetEditor } from '../services/usePresetEditor'

const editor = usePresetEditor('generative')
const { navigate } = useNavigation()

const showApiKey = ref(false)
const assistantPresetNames = ref<string[]>([])

void getClient().then(async (client) => {
  assistantPresetNames.value = (await client.presetLists()).assistants.presets.map((p) => p.name)
})

const baseUrlHelp: Record<ProviderType, string> = {
  local: 'Local default: http://localhost:7860 (Automatic1111-compatible).',
  openai: 'Default for OpenAI.',
  anthropic: '',
  google: 'Default for Google.',
  custom: 'Other compatible services, such as Stability or aggregators.',
}


</script>

<template>
  <main class="main">
    <Header title="Image Generation" subtitle="Providers and reusable presets used by Generative Fill. The active preset is preselected in the generation dialog." />
    <div class="body" style="flex-direction: row; flex-wrap: wrap; align-items: flex-start">
      <PresetEditorShell :editor="editor" list-label="Presets" kind-label="Image Generation">
        <template v-if="editor.draft.value">
          <Group title="Provider">
            <template #meta>
              <span v-if="editor.connectionResult.value" class="bdg" :class="editor.connectionResult.value.ok ? 'ok' : 'err'">
                {{ editor.connectionResult.value.ok ? 'Connected' : 'Failed' }}
              </span>
            </template>
            <div class="row">
              <span class="lbl">Provider Type</span>
              <div>
                <!-- Anthropic is absent on purpose: it has no image generation
                     API, so it is not offered here or in the New Preset dialog. -->
                <SegmentedControl
                  :model-value="editor.draft.value.provider.type"
                  aria-label="Provider type"
                  :options="[
                    { value: 'local', label: 'Local' },
                    { value: 'openai', label: 'OpenAI' },
                    { value: 'google', label: 'Google' },
                    { value: 'custom', label: 'Custom' },
                  ]"
                  @update:model-value="editor.setProviderType($event)"
                />
                <div class="help">{{ baseUrlHelp[editor.draft.value.provider.type] }}</div>
              </div>
            </div>
            <div class="row">
              <label class="lbl" for="bu">Base URL</label>
              <input id="bu" v-model="editor.draft.value.provider.baseUrl" class="in mono" />
            </div>
            <div class="row">
              <label class="lbl" for="ak">API Key</label>
              <div class="ctl" style="flex-wrap: nowrap">
                <input
                  id="ak"
                  v-model="editor.draft.value.provider.apiKey"
                  class="in mono"
                  :type="showApiKey ? 'text' : 'password'"
                />
                <button type="button" class="btn ib" aria-label="Show API key" title="Show API key" @click="showApiKey = !showApiKey">
                  <Icon name="eye" />
                </button>
              </div>
            </div>
            <div class="row">
              <label class="lbl" for="md">Model</label>
              <div>
                <div class="ctl" style="flex-wrap: nowrap">
                  <ComboField
                    v-model="editor.draft.value.provider.model"
                    input-id="md"
                    mono
                    :options="editor.detectedModels.value"
                    :list-label="`${editor.detectedModels.value.length} models detected`"
                    empty-label="Run Detect Models to list what the endpoint offers."
                  />
                  <button type="button" class="btn" :disabled="editor.detectingModels.value" @click="editor.detectModels()">
                    <Icon :name="editor.detectingModels.value ? 'spinner' : 'refresh'" :class="{ spin: editor.detectingModels.value }" />
                    {{ editor.detectingModels.value ? 'Detecting…' : 'Detect Models' }}
                  </button>
                </div>
                <div class="help">Detect Models is available for providers that expose a model list.</div>
              </div>
            </div>
            <div class="sep" />
            <div class="ctl">
              <button type="button" class="btn" :disabled="editor.testingConnection.value" @click="editor.testConnection()">
                <Icon v-if="editor.testingConnection.value" name="spinner" class="spin" />
                <Icon v-else name="plug" />
                {{ editor.testingConnection.value ? 'Testing…' : 'Test Connection' }}
              </button>
              <span v-if="editor.connectionResult.value" class="muted">{{ editor.connectionResult.value.message }}</span>
            </div>
          </Group>

          <Group title="Generation Defaults">
            <p class="grp-desc">Stored in this preset. The Generative Fill dialog can override any of these for a single generation.</p>
            <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 12px 20px">
              <div>
                <label class="flbl" for="st">Steps</label>
                <NumberField v-model="editor.draft.value.generative.steps" :min="1" :max="150" />
              </div>
              <div>
                <label class="flbl" for="sd">Seed</label>
                <NumberField v-model="editor.draft.value.generative.seed" unit="random" :min="-1" :max="2147483647" />
                <div class="help">-1 picks a new seed for each generation.</div>
              </div>
              <div style="grid-column: 1 / -1">
                <label class="flbl" for="sg">Strength</label>
                <NumberField v-model="editor.draft.value.generative.strength" slider :min="0" :max="1" :step="0.01" style="max-width: 520px" aria-label="Strength" />
                <div class="help">0.0 keeps the original pixels; 1.0 replaces the area completely.</div>
              </div>
              <div style="grid-column: 1 / -1">
                <label class="flbl" for="np">Negative Prompt</label>
                <textarea id="np" v-model="editor.draft.value.generative.negativePrompt" class="in" rows="2" />
              </div>
            </div>
          </Group>

          <Group title="Prompt Assistance">
            <div class="row" style="align-items: center">
              <span class="lbl" style="padding: 0">Enable Prompt Assistance</span>
              <Toggle v-model="editor.draft.value.generative.promptAssistEnabled" label="Enable Prompt Assistance">
                <span class="muted">An Assistant preset refines the prompt before it is sent</span>
              </Toggle>
            </div>
            <div class="row">
              <label class="lbl" for="ap">Assistant Preset</label>
              <div>
                <SelectField
                  id="ap"
                  v-model="editor.draft.value.generative.promptAssistPreset"
                  style="max-width: 360px"
                  :disabled="!editor.draft.value.generative.promptAssistEnabled"
                  :options="assistantPresetNames.map((name) => ({ value: name, label: name }))"
                />
                <div class="help">
                  Disabled while Prompt Assistance is off.
                  <a href="#" @click.prevent="navigate('assistants')">Manage Assistant presets</a>
                </div>
              </div>
            </div>
          </Group>
        </template>
      </PresetEditorShell>
    </div>
  </main>
</template>
