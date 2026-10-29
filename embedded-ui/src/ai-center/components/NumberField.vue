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

function onNumberInput(event: Event): void {
  const value = Number((event.target as HTMLInputElement).value)
  if (!Number.isNaN(value)) {
    emit('update:modelValue', value)
  }
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
