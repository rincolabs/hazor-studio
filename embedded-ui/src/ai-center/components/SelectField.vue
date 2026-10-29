<script setup lang="ts" generic="T extends string">
export interface SelectOption<T extends string> {
  value: T
  label: string
}

const props = defineProps<{
  modelValue: T
  options: Array<SelectOption<T>>
  disabled?: boolean
  mono?: boolean
  ariaLabel?: string
}>()

const emit = defineEmits<{ 'update:modelValue': [value: T] }>()
</script>

<template>
  <div class="selw">
    <select
      class="in"
      :class="{ mono: props.mono }"
      :value="props.modelValue"
      :disabled="props.disabled"
      :aria-label="props.ariaLabel"
      @change="emit('update:modelValue', ($event.target as HTMLSelectElement).value as T)"
    >
      <option v-for="option in props.options" :key="option.value" :value="option.value">
        {{ option.label }}
      </option>
    </select>
    <svg class="i chev" viewBox="0 0 24 24" aria-hidden="true"><path d="m6 9 6 6 6-6" /></svg>
  </div>
</template>
