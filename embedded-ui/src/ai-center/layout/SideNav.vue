<script setup lang="ts">
import Icon from '../components/Icon.vue'
import type { IconName } from '../components/icons'
import type { SectionId } from '../services/contract'

const props = defineProps<{
  current: SectionId
  alertCount: number
  runtimeLabel: string
  runtimeOk: boolean
  mcpLabel: string
  mcpOk: boolean
}>()

const emit = defineEmits<{ navigate: [section: SectionId] }>()

const items: Array<{ id: SectionId; label: string; icon: IconName }> = [
  { id: 'overview', label: 'Overview', icon: 'overview' },
  { id: 'assistants', label: 'Assistants', icon: 'assistants' },
  { id: 'image-generation', label: 'Image Generation', icon: 'imageGeneration' },
  { id: 'models', label: 'Models', icon: 'models' },
  { id: 'system', label: 'System', icon: 'system' },
]
</script>

<template>
  <nav class="side" aria-label="AI Center sections">
    <div class="brand">
      <span class="logo"><Icon name="brand" size="lg" /></span>
      <div>
        <div class="brand-t">Hazor AI Center</div>
        <div class="brand-s">Hazor Studio</div>
      </div>
    </div>

    <button
      v-for="item in items"
      :key="item.id"
      type="button"
      class="nav"
      :class="{ on: props.current === item.id }"
      :aria-current="props.current === item.id ? 'page' : undefined"
      @click="emit('navigate', item.id)"
    >
      <Icon :name="item.icon" size="lg" />
      {{ item.label }}
      <span v-if="item.id === 'overview' && props.alertCount > 0" class="bdg warn cnt">{{ props.alertCount }}</span>
    </button>

    <div class="side-foot">
      <div class="sf-row">
        <span class="dot" :style="{ color: props.runtimeOk ? 'var(--ok)' : 'var(--warn)' }" />
        {{ props.runtimeLabel }}
      </div>
      <div class="sf-row">
        <span class="dot" :style="{ color: props.mcpOk ? 'var(--ok)' : 'var(--text-3)' }" />
        {{ props.mcpLabel }}
      </div>
    </div>
  </nav>
</template>
