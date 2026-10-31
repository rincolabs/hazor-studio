<script setup lang="ts">
/** Fixed-height toolbar (never scrolls, never wraps). */
import Icon from '../ui/Icon.vue'
import StatusIndicator from './StatusIndicator.vue'
import type { StatusKind } from '../../services/status'

defineProps<{
  stateKind: StatusKind
  stateLabel: string
  detail: string
  progress: number | null
  busy: boolean
  canRun: boolean
  refreshing: boolean
}>()

const emit = defineEmits<{
  run: []
  cancel: []
  refreshModels: []
  add: [button: HTMLElement]
}>()
</script>

<template>
  <header class="tb" role="toolbar" aria-label="Neural Flow">
    <div class="tb-left">
      <span class="tb-logo"><Icon name="flow" :size="15" /></span>
      <h1 class="tb-title">Neural Flow</h1>
    </div>

    <div class="tb-right">
      <StatusIndicator :kind="stateKind" :label="stateLabel" :detail="detail" :progress="progress" />
      <span class="tb-div" />
      <button
        class="btn ghost"
        type="button"
        title="Add Node (Shift A)"
        @click="emit('add', $event.currentTarget as HTMLElement)"
      >
        <Icon name="plus" /><span class="tb-lbl">Add</span>
      </button>
      <button class="btn" type="button" :disabled="refreshing || busy" @click="emit('refreshModels')">
        <Icon name="refresh" :class="{ spin: refreshing }" /><span class="tb-lbl">{{
          refreshing ? 'Refreshing…' : 'Refresh Models'
        }}</span>
      </button>
      <button v-if="busy" class="btn tb-run" type="button" @click="emit('cancel')">
        <Icon name="stop" /><span class="tb-lbl">Cancel</span>
      </button>
      <button v-else class="btn pri tb-run" type="button" :disabled="!canRun" @click="emit('run')">
        <Icon name="play" /><span class="tb-lbl">Run</span>
      </button>
    </div>
  </header>
</template>

<style scoped>
.tb {
  flex: none;
  height: var(--toolbar-h);
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  padding: 0 8px 0 12px;
  background: var(--bg-4);
  border-bottom: 1px solid var(--bg-0);
  overflow: hidden;
  position: relative;
  z-index: 10;
}
.tb-left,
.tb-right {
  display: flex;
  align-items: center;
  gap: 8px;
  min-width: 0;
}
.tb-left {
  flex: 1 1 auto;
}
.tb-right {
  flex: 0 1 auto;
  justify-content: flex-end;
}
.tb-logo {
  width: 24px;
  height: 24px;
  border-radius: 4px;
  flex: none;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  background: var(--surface);
  color: var(--accent-t);
}
.tb-title {
  margin: 0;
  font-size: 13px;
  font-weight: 600;
  color: var(--text);
  white-space: nowrap;
  letter-spacing: -0.005em;
}
.tb-div {
  width: 1px;
  height: 18px;
  background: var(--line);
  opacity: 0.6;
  flex: none;
}
.tb-run {
  min-width: 72px;
}
@media (max-width: 760px) {
  .tb-lbl {
    display: none;
  }
  .tb .btn {
    width: var(--control-h);
    padding: 0;
    min-width: 0;
  }
}
</style>
