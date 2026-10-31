<script setup lang="ts">
import Icon from '../ui/Icon.vue'
import type { StatusKind } from '../../services/status'

defineProps<{
  kind: StatusKind
  label: string
  detail: string
  progress: number | null
}>()
</script>

<template>
  <div class="st" :class="kind" role="status" aria-live="polite" :title="detail || label">
    <Icon v-if="kind === 'busy'" class="spin" name="loader" :size="13" />
    <span v-else class="dot" />
    <span class="st-label">{{ label }}</span>
    <span v-if="detail" class="st-detail">{{ detail }}</span>
    <span v-if="kind === 'busy' && progress != null" class="st-bar" aria-hidden="true">
      <span :style="{ width: `${progress * 100}%` }" />
    </span>
  </div>
</template>

<style scoped>
.st {
  display: inline-flex;
  align-items: center;
  gap: 7px;
  height: 26px;
  padding: 0 10px;
  border-radius: 13px;
  background: var(--bg-3);
  color: var(--text-2);
  min-width: 0;
  max-width: 340px;
}
.st .dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  background: currentColor;
  flex: none;
}
.st.idle .dot {
  color: var(--text-3);
}
.st.ok .dot {
  color: var(--ok);
}
.st-label {
  font-weight: 600;
  color: var(--text);
}
.st-detail {
  color: var(--text-3);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  min-width: 0;
}
.st.busy {
  background: var(--info-bg);
}
.st.busy .i {
  color: var(--info);
}
.st.error {
  background: var(--err-bg);
}
.st.error .dot,
.st.error .st-label {
  color: var(--err);
}
.st.error .st-detail {
  color: #f5c9c3;
}
.st-bar {
  width: 64px;
  height: 4px;
  border-radius: 2px;
  background: rgba(255, 255, 255, 0.12);
  overflow: hidden;
  flex: none;
}
.st-bar > span {
  display: block;
  height: 100%;
  background: var(--info);
  transition: width 0.2s;
}
@media (max-width: 900px) {
  .st-detail,
  .st-bar {
    display: none;
  }
}
</style>
