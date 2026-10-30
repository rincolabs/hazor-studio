<script setup lang="ts">
import Banner from '../../components/Banner.vue'
import Group from '../../components/Group.vue'
import NumberField from '../../components/NumberField.vue'
import SelectField from '../../components/SelectField.vue'
import Toggle from '../../components/Toggle.vue'
import type { PresetEditor } from '../../services/usePresetEditor'
import TestPromptGroup from './TestPromptGroup.vue'

defineProps<{ editor: PresetEditor }>()
</script>

<template>
  <template v-if="editor.draft.value">
    <Banner severity="info">Advanced options for this preset. The defaults work for most providers — change them only when troubleshooting.</Banner>

    <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); gap: 14px">
      <Group title="Requests">
        <div class="row" style="grid-template-columns: 150px minmax(0, 1fr)">
          <label class="lbl" for="to">Request Timeout</label>
          <div>
            <NumberField v-model="editor.draft.value.timeoutMs" unit="ms" :min="1000" :max="300000" />
            <div class="help">1000–300000 ms. Default 30000 ms.</div>
          </div>
        </div>
        <div class="row" style="grid-template-columns: 150px minmax(0, 1fr)">
          <label class="lbl" for="re">Retries</label>
          <div>
            <NumberField v-model="editor.draft.value.retries" :min="0" :max="10" />
            <div class="help">0–10 additional attempts after a failed request.</div>
          </div>
        </div>
        <div class="row" style="grid-template-columns: 150px minmax(0, 1fr); align-items: center">
          <span class="lbl" style="padding: 0">Streaming</span>
          <Toggle v-model="editor.draft.value.stream" label="Streaming">
            <span class="muted">{{ editor.draft.value.stream ? 'On — responses stream as they arrive' : 'Off — responses arrive complete' }}</span>
          </Toggle>
        </div>
      </Group>

      <Group title="Conversation">
        <div class="row" style="grid-template-columns: 150px minmax(0, 1fr)">
          <label class="lbl" for="mh">Maximum History Turns</label>
          <div>
            <NumberField v-model="editor.draft.value.maxHistoryTurns" unit="turns" :min="0" :max="200" />
            <div class="help">Older turns are dropped from the context.</div>
          </div>
        </div>
        <div class="row" style="grid-template-columns: 150px minmax(0, 1fr); align-items: center">
          <span class="lbl" style="padding: 0">Cache</span>
          <Toggle v-model="editor.draft.value.cache" label="Cache">
            <span class="muted">{{ editor.draft.value.cache ? 'On' : 'Off' }}</span>
          </Toggle>
        </div>
        <div class="row" style="grid-template-columns: 150px minmax(0, 1fr); align-items: center">
          <span class="lbl" style="padding: 0">Result Feedback</span>
          <Toggle v-model="editor.draft.value.resultFeedback" label="Result feedback">
            <span class="muted">Tool results are sent back to the model</span>
          </Toggle>
        </div>
      </Group>

      <Group title="Logging">
        <div class="row" style="grid-template-columns: 150px minmax(0, 1fr)">
          <label class="lbl" for="ll">Log Level</label>
          <div>
            <SelectField
              id="ll"
              v-model="editor.draft.value.logLevel"
              style="max-width: 180px"
              :options="[
                { value: 'info', label: 'info' },
                { value: 'debug', label: 'debug' },
                { value: 'warning', label: 'warning' },
                { value: 'error', label: 'error' },
              ]"
            />
            <div class="help">Stored with the preset. Agent logging reads it from here once wired up.</div>
          </div>
        </div>
      </Group>
    </div>

    <TestPromptGroup :editor="editor" />
  </template>
</template>
