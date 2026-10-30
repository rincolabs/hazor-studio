<script setup lang="ts">
import { ref } from 'vue'

import ComboField from '../../components/ComboField.vue'
import Group from '../../components/Group.vue'
import Icon from '../../components/Icon.vue'
import SegmentedControl from '../../components/SegmentedControl.vue'
import type { ProviderType } from '../../services/contract'
import type { PresetEditor } from '../../services/usePresetEditor'
import TestPromptGroup from './TestPromptGroup.vue'

defineProps<{ editor: PresetEditor }>()

const showApiKey = ref(false)

const baseUrlHelp: Record<ProviderType, string> = {
  local: 'Local default: http://localhost:11434',
  openai: 'Default for OpenAI.',
  anthropic: 'Default for Anthropic.',
  google: 'Default for Google.',
  custom: 'Enter the OpenAI-compatible endpoint for this service.',
}

</script>

<template>
  <template v-if="editor.draft.value">
    <Group title="Connection">
      <div class="row">
        <span class="lbl">Provider Type</span>
        <div>
          <!-- Not v-model: switching type re-prefills Base URL and API Key from
               ProviderConfig's defaults, which the editor has to ask C++ for. -->
          <SegmentedControl
            :model-value="editor.draft.value.provider.type"
            aria-label="Provider type"
            @update:model-value="editor.setProviderType($event)"
            :options="[
              { value: 'local', label: 'Local' },
              { value: 'openai', label: 'OpenAI' },
              { value: 'anthropic', label: 'Anthropic' },
              { value: 'google', label: 'Google' },
              { value: 'custom', label: 'Custom' },
            ]"
          />
          <div class="help">Local targets Ollama or llama.cpp endpoints. Custom accepts OpenAI-compatible third-party endpoints.</div>
        </div>
      </div>
      <div class="row">
        <label class="lbl" for="bu">Base URL</label>
        <div>
          <input id="bu" v-model="editor.draft.value.provider.baseUrl" class="in mono" />
          <div class="help">{{ baseUrlHelp[editor.draft.value.provider.type] }}</div>
        </div>
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
              placeholder="Model name (e.g. qwen2.5:3b)"
              :options="editor.detectedModels.value"
              :list-label="`${editor.detectedModels.value.length} models detected`"
              empty-label="Run Detect Models to list what the endpoint offers."
            />
            <button type="button" class="btn" :disabled="editor.detectingModels.value" @click="editor.detectModels()">
              <Icon :name="editor.detectingModels.value ? 'spinner' : 'refresh'" :class="{ spin: editor.detectingModels.value }" />
              {{ editor.detectingModels.value ? 'Detecting…' : 'Detect Models' }}
            </button>
          </div>
          <div class="help">
            Type a model ID or pick one detected from the endpoint.
            <template v-if="editor.detectedModels.value.length"> {{ editor.detectedModels.value.length }} models detected.</template>
          </div>
        </div>
      </div>
      <div class="sep" />
      <div class="ctl">
        <button type="button" class="btn" :disabled="editor.testingConnection.value" @click="editor.testConnection()">
          <Icon v-if="editor.testingConnection.value" name="spinner" class="spin" />
          <Icon v-else name="plug" />
          {{ editor.testingConnection.value ? 'Testing…' : 'Test Connection' }}
        </button>
        <template v-if="editor.connectionResult.value">
          <span class="bdg" :class="editor.connectionResult.value.ok ? 'ok' : 'err'">
            <Icon :name="editor.connectionResult.value.ok ? 'check' : 'close'" style="width: 12px; height: 12px" />
            {{ editor.connectionResult.value.ok ? 'Connected' : 'Failed' }}
          </span>
          <span class="muted">{{ editor.connectionResult.value.message }}</span>
        </template>
      </div>
    </Group>

    <TestPromptGroup :editor="editor" :default-open="true" />
  </template>
</template>
