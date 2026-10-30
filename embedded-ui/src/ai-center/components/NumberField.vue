<script setup lang="ts">
import { computed } from 'vue'

const props = defineProps<{
  modelValue: number
  unit?: string
  min?: number
  max?: number
  step?: number
  disabled?: boolean
  invalid?: boolean
  /** Renders a paired <input type="range"> in front of the number box. */
  slider?: boolean
  ariaLabel?: string
}>()

const emit = defineEmits<{ 'update:modelValue': [value: number] }>()

const display = computed(() => String(props.modelValue))

function clamp(value: number): number {
  let next = value
  if (props.min !== undefined) {
    next = Math.max(props.min, next)
  }
  if (props.max !== undefined) {
    next = Math.min(props.max, next)
  }
  return next
}

/**
 * The number box is a text input (no browser spinners in this design system),
 * so `min`/`max` have to be enforced here — they used to bind to the range
 * input only, which let a typed value sail past them into QSettings. Writing
 * the clamped value back to the element matters: when it clamps to what
 * `modelValue` already was, nothing re-renders and the box would keep showing
 * what was typed.
 */
function onNumberInput(event: Event): void {
  const input = event.target as HTMLInputElement
  const parsed = Number(input.value)
  if (input.value.trim() === '' || Number.isNaN(parsed)) {
    input.value = display.value
    return
  }
  const next = clamp(parsed)
  input.value = String(next)
  emit('update:modelValue', next)
}
</script>

<template>
  <div class="slider" v-if="props.slider">
    <input
      type="range"
      :min="props.min"
      :max="props.max"
      :step="props.step"
      :value="props.modelValue"
      :disabled="props.disabled"
      :aria-label="props.ariaLabel"
      @input="onNumberInput"
    />
    <div class="num" :class="{ off: props.disabled }">
      <input
        class="in"
        :class="{ bad: props.invalid }"
        :value="display"
        :disabled="props.disabled"
        :aria-invalid="props.invalid || undefined"
        @change="onNumberInput"
      />
      <span v-if="props.unit" class="unit">{{ props.unit }}</span>
    </div>
  </div>
  <div v-else class="num" :class="{ off: props.disabled }" :style="props.invalid ? 'border-color:#d0675c' : undefined">
    <input
      class="in"
      :value="display"
      :disabled="props.disabled"
      :aria-invalid="props.invalid || undefined"
      :aria-label="props.ariaLabel"
      @change="onNumberInput"
    />
    <span v-if="props.unit" class="unit">{{ props.unit }}</span>
  </div>
</template>
