<script setup lang="ts">
import Group from '../../components/Group.vue'
import NumberField from '../../components/NumberField.vue'
import type { PresetEditor } from '../../services/usePresetEditor'
import TestPromptGroup from './TestPromptGroup.vue'

const props = defineProps<{ editor: PresetEditor }>()

const defaults = { temperature: 0.2, topP: 0.9, frequencyPenalty: 0.0, presencePenalty: 0.0 }

function resetDefaults(): void {
  if (props.editor.draft.value) {
    Object.assign(props.editor.draft.value.modelSettings, defaults)
  }
}
</script>

<template>
  <template v-if="editor.draft.value">
    <Group title="Sampling">
      <template #meta><button type="button" class="btn ghost" style="height: 22px" @click="resetDefaults">Reset to defaults</button></template>
      <div class="row">
        <label class="lbl" for="t">Temperature</label>
        <div>
          <NumberField v-model="editor.draft.value.modelSettings.temperature" slider :min="0" :max="2" :step="0.05" aria-label="Temperature" />
          <div class="help">
            <template v-if="editor.draft.value.modelSettings.temperature !== defaults.temperature">
              <span class="warn-t">Modified · default {{ defaults.temperature.toFixed(1) }}.</span>
            </template>
            Lower values make answers more deterministic. Default 0.2 suits tool-driven editing tasks.
          </div>
        </div>
      </div>
      <div class="row">
        <label class="lbl" for="tpp">Top P</label>
        <div>
          <NumberField v-model="editor.draft.value.modelSettings.topP" slider :min="0" :max="1" :step="0.01" aria-label="Top P" />
          <div class="help">Nucleus sampling — only tokens within the top P probability mass are considered.</div>
        </div>
      </div>
      <div class="row">
        <label class="lbl" for="fp">Frequency Penalty</label>
        <div>
          <NumberField v-model="editor.draft.value.modelSettings.frequencyPenalty" slider :min="-2" :max="2" :step="0.1" aria-label="Frequency penalty" />
          <div class="help">Positive values reduce verbatim repetition. Ignored by providers that don't support it.</div>
        </div>
      </div>
      <div class="row">
        <label class="lbl" for="pp">Presence Penalty</label>
        <div>
          <NumberField v-model="editor.draft.value.modelSettings.presencePenalty" slider :min="-2" :max="2" :step="0.1" aria-label="Presence penalty" />
          <div class="help">
            <template v-if="editor.draft.value.modelSettings.presencePenalty !== defaults.presencePenalty">
              <span class="warn-t">Modified · default {{ defaults.presencePenalty.toFixed(1) }}.</span>
            </template>
            Positive values encourage new topics.
          </div>
        </div>
      </div>
    </Group>

    <Group title="Output">
      <div class="row">
        <label class="lbl" for="mt">Max Tokens</label>
        <div>
          <NumberField v-model="editor.draft.value.modelSettings.maxTokens" unit="tokens" :min="1" />
          <div class="help">Upper limit for a single response. The model's own limit still applies.</div>
        </div>
      </div>
    </Group>

    <TestPromptGroup :editor="editor" />
  </template>
</template>
