<script setup lang="ts">
import { onBeforeUnmount, onMounted } from 'vue'

import Icon from './Icon.vue'

const props = defineProps<{
  title: string
  /** Alert dialogs (confirmations) skip the header close button. */
  alert?: boolean
}>()

const emit = defineEmits<{ close: [] }>()

function onKeydown(event: KeyboardEvent): void {
  if (event.key === 'Escape') {
    emit('close')
  }
}

onMounted(() => window.addEventListener('keydown', onKeydown))
onBeforeUnmount(() => window.removeEventListener('keydown', onKeydown))
</script>

<template>
  <div class="dlg-backdrop" @mousedown.self="emit('close')">
    <div class="dlg" :role="props.alert ? 'alertdialog' : 'dialog'" :aria-label="props.title">
      <div class="dlg-h">
        {{ props.title }}
        <button
          v-if="!props.alert"
          type="button"
          class="btn ghost ib"
          aria-label="Close"
          @click="emit('close')"
        >
          <Icon name="close" />
        </button>
      </div>
      <div class="dlg-b">
        <slot />
      </div>
      <div class="dlg-f">
        <slot name="footer" />
      </div>
    </div>
  </div>
</template>
