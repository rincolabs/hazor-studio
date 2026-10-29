<script setup lang="ts">
import Dialog from '../components/Dialog.vue'

defineProps<{ presetName: string }>()
const emit = defineEmits<{ discard: []; cancel: []; saveAndSwitch: [] }>()
</script>

<template>
  <Dialog :title="`Save changes to “${presetName}”?`" alert @close="emit('cancel')">
    <p style="margin: 0">This preset has unsaved changes.</p>
    <template #footer>
      <button type="button" class="btn ghost danger" style="margin-right: auto" @click="emit('discard')">Discard</button>
      <button type="button" class="btn ghost" @click="emit('cancel')">Cancel</button>
      <button type="button" class="btn pri" @click="emit('saveAndSwitch')">Save &amp; Switch</button>
    </template>
  </Dialog>
</template>
