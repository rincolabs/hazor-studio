<script setup lang="ts">
/**
 * Free-text field with a popup list of suggestions — an editable combobox, the
 * web equivalent of the `QComboBox(editable, NoInsert)` the old Qt page used
 * for the model name.
 *
 * Deliberately not an `<input list>` + `<datalist>`: that popup only offers the
 * options matching what is already in the field, so a detected list of 50
 * models shows as three rows, and it is painted by the engine rather than by
 * this design system.
 */
import { nextTick, onBeforeUnmount, ref, watch } from 'vue'

import Icon from './Icon.vue'

const props = defineProps<{
  modelValue: string
  options: string[]
  inputId?: string
  placeholder?: string
  /** Shown above the list, e.g. "12 models detected". */
  listLabel?: string
  emptyLabel?: string
  disabled?: boolean
  mono?: boolean
  ariaLabel?: string
}>()

const emit = defineEmits<{ 'update:modelValue': [value: string] }>()

const root = ref<HTMLElement | null>(null)
const field = ref<HTMLInputElement | null>(null)
const open = ref(false)
const highlighted = ref(-1)

function close(): void {
  open.value = false
  highlighted.value = -1
}

function toggle(): void {
  if (props.disabled) return
  if (open.value) {
    close()
    return
  }
  open.value = true
  // Start on the current value so Enter re-picks it rather than the first row.
  highlighted.value = props.options.indexOf(props.modelValue)
}

function pick(option: string): void {
  emit('update:modelValue', option)
  close()
  void nextTick(() => field.value?.focus())
}

function move(delta: number): void {
  if (!open.value) {
    toggle()
    return
  }
  if (props.options.length === 0) return
  const next = highlighted.value + delta
  highlighted.value = (next + props.options.length) % props.options.length
}

function onKeydown(event: KeyboardEvent): void {
  switch (event.key) {
    case 'ArrowDown':
      event.preventDefault()
      move(1)
      break
    case 'ArrowUp':
      event.preventDefault()
      move(-1)
      break
    case 'Enter':
      if (open.value && highlighted.value >= 0) {
        event.preventDefault()
        pick(props.options[highlighted.value])
      }
      break
    case 'Escape':
      if (open.value) {
        // Stop here: an Escape that closed the list is not an Escape for
        // whatever else on the page listens for it.
        event.stopPropagation()
        close()
      }
      break
    default:
      break
  }
}

function onPointerDown(event: PointerEvent): void {
  if (root.value && !root.value.contains(event.target as Node)) {
    close()
  }
}

watch(open, (isOpen) => {
  if (isOpen) {
    document.addEventListener('pointerdown', onPointerDown, true)
  } else {
    document.removeEventListener('pointerdown', onPointerDown, true)
  }
})

onBeforeUnmount(() => {
  document.removeEventListener('pointerdown', onPointerDown, true)
})
</script>

<template>
  <div ref="root" class="cmb">
    <input
      :id="props.inputId"
      ref="field"
      class="in"
      :class="{ mono: props.mono }"
      role="combobox"
      aria-autocomplete="list"
      :aria-expanded="open"
      :aria-label="props.ariaLabel"
      :value="props.modelValue"
      :placeholder="props.placeholder"
      :disabled="props.disabled"
      @input="emit('update:modelValue', ($event.target as HTMLInputElement).value)"
      @keydown="onKeydown"
    />
    <button
      type="button"
      class="cmb-tog"
      tabindex="-1"
      :disabled="props.disabled"
      :aria-label="open ? 'Hide suggestions' : 'Show suggestions'"
      :title="open ? 'Hide suggestions' : 'Show suggestions'"
      @click="toggle"
    >
      <Icon :name="open ? 'chevronUp' : 'chevronDown'" />
    </button>

    <div v-if="open" class="cmb-pop" role="listbox">
      <div v-if="props.listLabel" class="cmb-h">{{ props.listLabel }}</div>
      <button
        v-for="(option, index) in props.options"
        :key="option"
        type="button"
        class="cmb-opt"
        :class="{ on: option === props.modelValue, hl: index === highlighted }"
        role="option"
        :aria-selected="option === props.modelValue"
        @click="pick(option)"
        @mousemove="highlighted = index"
      >
        <Icon name="check" />
        <span class="mono">{{ option }}</span>
      </button>
      <div v-if="props.options.length === 0" class="cmb-h" style="border: 0; margin: 0">
        {{ props.emptyLabel ?? 'No suggestions yet.' }}
      </div>
    </div>
  </div>
</template>
