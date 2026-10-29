<script setup lang="ts">
import { computed } from 'vue'

import Dialog from '../components/Dialog.vue'
import type { PresetEditor } from '../services/usePresetEditor'

const props = defineProps<{ editor: PresetEditor }>()
const emit = defineEmits<{ close: [] }>()

const isActive = computed(
  () => props.editor.list.value.presets.find((p) => p.name === props.editor.selectedName.value)?.isActive ?? false,
)

async function onConfirm(): Promise<void> {
  await props.editor.remove()
  emit('close')
}
</script>

<template>
  <Dialog :title="isActive ? 'Delete the active preset?' : `Delete “${editor.selectedName.value}”?`" alert @close="emit('close')">
    <p v-if="isActive" style="margin: 0">
      “{{ editor.selectedName.value }}” is currently active. Choose another active preset after deleting it.
    </p>
    <p v-else style="margin: 0">This removes the preset and all of its settings. This can't be undone.</p>
    <template #footer>
      <button type="button" class="btn ghost" @click="emit('close')">Cancel</button>
      <button type="button" class="btn danger-fill" @click="onConfirm">{{ isActive ? 'Delete Anyway' : 'Delete Preset' }}</button>
    </template>
  </Dialog>
</template>
