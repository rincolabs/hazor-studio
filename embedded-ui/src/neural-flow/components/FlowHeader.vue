<script setup lang="ts">
/**
 * The compact bar across the top: what the editor is, what the run is doing,
 * and the two actions that drive it.
 */
import type { StatusKind } from '../services/status'

defineProps<{
  /** One word for the run state, already translated to something readable. */
  stateLabel: string
  stateKind: StatusKind
  /** The rest of the sentence: a model name, a step count, an error. */
  detail: string
  busy: boolean
  canRun: boolean
  /** Compiled backend, devices found and device in use, in one line. */
  backend: string
}>()

const emit = defineEmits<{
  run: []
  cancel: []
  refreshModels: []
}>()
</script>

<template>
  <header class="flow-header">
    <div class="flow-header__identity">
      <strong class="flow-header__title">Neural Flow</strong>
      <span class="flow-header__backend" :title="backend">{{ backend }}</span>
    </div>

    <div class="flow-header__status" :class="`flow-header__status--${stateKind}`" role="status">
      <span class="flow-header__dot" aria-hidden="true"></span>
      <span class="flow-header__state">{{ stateLabel }}</span>
      <span v-if="detail" class="flow-header__detail">{{ detail }}</span>
    </div>

    <div class="flow-header__actions">
      <button type="button" :disabled="busy" @click="emit('refreshModels')">
        Refresh models
      </button>
      <button v-if="busy" type="button" class="flow-header__stop" @click="emit('cancel')">
        Cancel
      </button>
      <button
        v-else
        type="button"
        class="flow-header__run"
        :disabled="!canRun"
        @click="emit('run')"
      >
        Run
      </button>
    </div>
  </header>
</template>

<style scoped>
.flow-header {
  display: flex;
  align-items: center;
  gap: var(--hz-space-3);
  padding: var(--hz-space-2) var(--hz-space-3);
  background: var(--hz-surface);
  border-bottom: 1px solid var(--hz-border);
}

.flow-header__identity {
  display: flex;
  align-items: baseline;
  gap: var(--hz-space-2);
  min-width: 0;
}

.flow-header__title {
  color: var(--hz-text-bright);
  font-size: var(--hz-font-size-lg);
  white-space: nowrap;
}

.flow-header__backend {
  color: var(--hz-text-disabled);
  font-family: var(--hz-font-family-mono);
  font-size: var(--hz-font-size-sm);
  /* One line whatever the machine reports; the whole string is in the tooltip. */
  max-width: 22ch;
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;
}

.flow-header__status {
  display: flex;
  align-items: baseline;
  gap: var(--hz-space-2);
  min-width: 0;
  flex: 1;
}

.flow-header__dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  background: var(--hz-text-disabled);
  flex: none;
  align-self: center;
}

.flow-header__status--busy .flow-header__dot {
  background: var(--hz-accent);
}

.flow-header__status--ok .flow-header__dot {
  background: var(--hz-success);
}

.flow-header__status--error .flow-header__dot {
  background: var(--hz-danger);
}

.flow-header__state {
  color: var(--hz-text);
  white-space: nowrap;
}

.flow-header__status--error .flow-header__state {
  color: var(--hz-danger);
}

.flow-header__detail {
  color: var(--hz-text-secondary);
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;
}

.flow-header__actions {
  display: flex;
  gap: var(--hz-space-2);
  flex: none;
}

.flow-header__run:not(:disabled) {
  background: var(--hz-accent);
  border-color: var(--hz-accent);
  color: var(--hz-text-bright);
}

.flow-header__run:not(:disabled):hover {
  background: var(--hz-accent-hover);
}

.flow-header__stop {
  border-color: var(--hz-danger);
  color: var(--hz-danger);
}
</style>
