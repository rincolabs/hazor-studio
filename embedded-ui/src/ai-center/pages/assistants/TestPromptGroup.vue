<script setup lang="ts">
import { ref } from 'vue'

import Group from '../../components/Group.vue'
import Icon from '../../components/Icon.vue'
import type { PresetEditor } from '../../services/usePresetEditor'

const props = defineProps<{ editor: PresetEditor; defaultOpen?: boolean }>()

const prompt = ref('List the layers in the active document and suggest clearer names for the unnamed ones.')

async function run(): Promise<void> {
  await props.editor.testPrompt(prompt.value)
}
</script>

<template>
  <Group title="Test Prompt" collapsible :default-open="props.defaultOpen">
    <template v-if="!props.defaultOpen" #meta>
      Last run: {{ editor.promptResult.value ? (editor.promptResult.value.ok ? 'Completed' : 'Error') : '—' }}
    </template>
    <template v-else #meta>Testing utility — not the assistant chat</template>

    <div>
      <label class="flbl" for="tp">Prompt</label>
      <textarea id="tp" v-model="prompt" class="in" rows="3" />
    </div>
    <div class="ctl">
      <button type="button" class="btn pri" :disabled="editor.testingPrompt.value" @click="run">
        <Icon v-if="editor.testingPrompt.value" name="spinner" class="spin" />
        <Icon v-else name="play" />
        {{ editor.testingPrompt.value ? 'Running…' : 'Run' }}
      </button>
      <span v-if="editor.promptResult.value" class="bdg" :class="editor.promptResult.value.ok ? 'ok' : 'err'">
        {{ editor.promptResult.value.ok ? 'Completed' : 'Error' }}
      </span>
      <span v-if="editor.promptResult.value?.ok" class="muted">
        {{ editor.promptResult.value.elapsedSeconds.toFixed(1) }} s · uses this preset's current values
      </span>
    </div>
    <div v-if="editor.promptResult.value" class="out" :class="{ mono: !editor.promptResult.value.ok }" :style="!editor.promptResult.value.ok ? 'color:var(--err)' : undefined">
      {{ editor.promptResult.value.ok ? editor.promptResult.value.response : editor.promptResult.value.message }}
    </div>
    <div v-else>
      <span class="flbl">Response (read-only)</span>
      <div class="out" role="textbox" aria-readonly="true" aria-label="Test response" style="color: #8c8e8f; font-style: italic">
        Run a prompt to see the response here.
      </div>
    </div>
  </Group>
</template>
