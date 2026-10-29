<script setup lang="ts">
import Icon from './Icon.vue'
import type { PresetSummary, ProviderType } from '../services/contract'

const props = defineProps<{
  label: string
  presets: PresetSummary[]
  selectedName: string
  /** Name of the preset currently open for edit with unsaved changes. */
  dirtyName?: string
}>()

const emit = defineEmits<{
  select: [name: string]
  new: []
  duplicate: []
  delete: []
}>()

const providerLabel: Record<ProviderType, string> = {
  local: 'Local',
  openai: 'OpenAI',
  anthropic: 'Anthropic',
  google: 'Google',
  custom: 'Custom',
}
</script>

<template>
  <aside class="pl" aria-label="Presets" style="flex: 1 1 240px; max-width: 280px">
    <div class="pl-h">
      {{ props.label }}
      <span class="muted" style="font-weight: 500">· {{ props.presets.length }}</span>
      <span class="sp" />
      <button type="button" class="btn ghost ib" aria-label="New preset" title="New preset" @click="emit('new')">
        <Icon name="plus" />
      </button>
      <button
        type="button"
        class="btn ghost ib"
        aria-label="Duplicate preset"
        title="Duplicate preset"
        :disabled="!props.selectedName"
        @click="emit('duplicate')"
      >
        <Icon name="duplicate" />
      </button>
      <button
        type="button"
        class="btn ghost ib danger"
        aria-label="Delete preset"
        title="Delete preset"
        :disabled="!props.selectedName"
        @click="emit('delete')"
      >
        <Icon name="trash" />
      </button>
    </div>
    <div class="pl-list">
      <button
        v-for="preset in props.presets"
        :key="preset.name"
        type="button"
        class="pi"
        :class="{ on: preset.name === props.selectedName }"
        :aria-current="preset.name === props.selectedName ? 'true' : undefined"
        @click="emit('select', preset.name)"
      >
        <span class="pi-t">
          {{ preset.name }}
          <span v-if="preset.name === props.dirtyName" class="bdg warn" title="Unsaved changes">
            <span class="dot" />Edited
          </span>
          <span v-else-if="preset.isActive" class="bdg ok">Active</span>
        </span>
        <span class="pi-s">{{ providerLabel[preset.providerType] }} · {{ preset.model }}</span>
      </button>
    </div>
  </aside>
</template>
