<script setup lang="ts">
export interface TabItem {
  id: string
  label: string
  badge?: { text: string; kind: 'ok' | 'warn' | 'err' | 'info' }
}

const props = defineProps<{
  modelValue: string
  items: TabItem[]
  ariaLabel: string
}>()

const emit = defineEmits<{ 'update:modelValue': [value: string] }>()
</script>

<template>
  <nav class="tabs" :aria-label="props.ariaLabel">
    <button
      v-for="item in props.items"
      :key="item.id"
      type="button"
      class="tab"
      :class="{ on: item.id === props.modelValue }"
      :aria-current="item.id === props.modelValue ? 'page' : undefined"
      @click="emit('update:modelValue', item.id)"
    >
      {{ item.label }}
      <span v-if="item.badge" class="bdg" :class="item.badge.kind" style="height: 16px">{{ item.badge.text }}</span>
    </button>
  </nav>
</template>
