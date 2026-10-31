<script setup lang="ts">
import type { MenuItem } from '../../composables/useMenu'
import Icon from '../ui/Icon.vue'

const props = defineProps<{
  items: MenuItem[]
  level: number
  activeIndex: number
  openIndex: number
  query: string
  title: string
  minWidth: number
}>()
const emit = defineEmits<{ enter: [index: number]; select: [index: number] }>()

function parts(label: string): Array<{ t: string; m: boolean }> {
  const q = props.query.trim().toLowerCase()
  if (!q) return [{ t: label, m: false }]
  const i = label.toLowerCase().indexOf(q)
  if (i < 0) return [{ t: label, m: false }]
  return [
    { t: label.slice(0, i), m: false },
    { t: label.slice(i, i + q.length), m: true },
    { t: label.slice(i + q.length), m: false },
  ]
}
</script>

<template>
  <div
    class="mn-panel scroll-y"
    role="menu"
    :style="{ minWidth: `${Math.max(minWidth, level === 0 ? 220 : 196)}px` }"
    @mousedown.stop
    @wheel.stop
    @contextmenu.prevent
  >
    <div v-if="title" class="mn-title">
      <span>{{ title }}</span>
      <slot name="title-extra" />
    </div>
    <slot name="top" />

    <template v-for="(item, i) in items" :key="i">
      <div v-if="item.separator" class="mn-sep" role="separator" />
      <div v-else-if="item.empty" class="mn-empty">{{ item.label }}</div>
      <button
        v-else
        type="button"
        class="mn-item"
        role="menuitem"
        :data-idx="i"
        :class="{ active: i === activeIndex, open: i === openIndex }"
        :disabled="item.disabled"
        :aria-haspopup="item.children ? 'menu' : undefined"
        :aria-expanded="item.children ? i === openIndex : undefined"
        tabindex="-1"
        @mouseenter="emit('enter', i)"
        @click="emit('select', i)"
      >
        <span class="mn-ic">
          <Icon v-if="item.checked" name="check" :size="13" />
          <Icon v-else-if="item.icon" :name="(item.icon as any)" :size="13" :style="{ color: item.iconColor }" />
        </span>
        <span class="mn-label">
          <span v-for="(p, k) in parts(item.label)" :key="k" :class="{ hit: p.m }">{{ p.t }}</span>
        </span>
        <span v-if="item.hint" class="mn-hint">{{ item.hint }}</span>
        <span v-if="item.swatch" class="mn-swatch" :style="{ background: item.swatch }" :title="item.swatchTitle" />
        <span v-if="item.shortcut" class="kbd">{{ item.shortcut }}</span>
        <Icon v-if="item.children" class="mn-chev" name="chevronRight" :size="12" />
      </button>
    </template>
  </div>
</template>

<style scoped>
.mn-panel {
  padding: 4px;
  max-height: calc(100vh - 8px);
  background: var(--raised);
  border: 1px solid var(--hover);
  border-radius: var(--r-card);
  box-shadow: var(--shadow-pop);
  color: var(--text-2);
  font-size: var(--fs);
}
.mn-title {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  height: 22px;
  padding: 0 6px 0 8px;
  color: var(--text-3);
  font-size: 10.5px;
  font-weight: 600;
  letter-spacing: 0.06em;
  text-transform: uppercase;
}
.mn-item {
  display: flex;
  align-items: center;
  gap: 8px;
  width: 100%;
  height: var(--menu-item-h);
  padding: 0 8px 0 6px;
  border: 0;
  border-radius: 3px;
  background: transparent;
  color: var(--text-2);
  text-align: left;
  cursor: default;
  white-space: nowrap;
}
.mn-item.open {
  background: rgba(255, 255, 255, 0.07);
  color: var(--text);
}
.mn-item.active {
  background: var(--accent);
  color: #fff;
}
.mn-item.active .mn-hint,
.mn-item.active .mn-chev {
  color: rgba(255, 255, 255, 0.8);
}
.mn-item.active .mn-ic .i {
  color: #fff !important;
}
.mn-item:disabled {
  color: var(--text-disabled);
}
.mn-ic {
  width: 14px;
  display: inline-flex;
  justify-content: center;
  flex: none;
}
.mn-label {
  flex: 1;
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
}
.mn-label .hit {
  color: #fff;
  font-weight: 600;
  text-decoration: underline;
  text-decoration-color: var(--accent-t);
  text-underline-offset: 2px;
}
.mn-hint {
  color: var(--text-3);
  font-size: 11px;
}
.mn-swatch {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  flex: none;
  box-shadow: 0 0 0 1px rgba(0, 0, 0, 0.35);
}
.mn-chev {
  color: var(--text-3);
  margin-right: -3px;
}
.mn-sep {
  height: 1px;
  margin: 4px 2px;
  background: var(--bg-4);
}
.mn-empty {
  padding: 6px 8px;
  color: var(--text-3);
  font-style: italic;
}
</style>
