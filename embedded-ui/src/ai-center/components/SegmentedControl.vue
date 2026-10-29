<script setup lang="ts" generic="T extends string">
export interface SegmentOption<T extends string> {
  value: T
  label: string
  disabled?: boolean
}

const props = defineProps<{
  modelValue: T
  options: Array<SegmentOption<T>>
  ariaLabel?: string
}>()

const emit = defineEmits<{ 'update:modelValue': [value: T] }>()
</script>

<template>
  <div class="seg" role="group" :aria-label="props.ariaLabel">
    <button
      v-for="option in props.options"
      :key="option.value"
      type="button"
      :aria-pressed="option.value === props.modelValue"
      :disabled="option.disabled"
      @click="emit('update:modelValue', option.value)"
    >
      {{ option.label }}
    </button>
  </div>
</template>
