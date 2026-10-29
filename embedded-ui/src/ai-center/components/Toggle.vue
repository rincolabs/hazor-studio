<script setup lang="ts">
const props = defineProps<{
  modelValue: boolean
  label?: string
  disabled?: boolean
}>()

const emit = defineEmits<{ 'update:modelValue': [value: boolean] }>()

function toggle(): void {
  if (!props.disabled) {
    emit('update:modelValue', !props.modelValue)
  }
}
</script>

<template>
  <div class="tgl">
    <button
      type="button"
      class="tg"
      role="switch"
      :aria-checked="props.modelValue"
      :aria-label="props.label"
      :disabled="props.disabled"
      @click="toggle"
    />
    <slot />
  </div>
</template>
