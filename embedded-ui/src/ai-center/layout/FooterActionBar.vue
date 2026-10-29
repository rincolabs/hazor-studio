<script setup lang="ts">
import Icon from '../components/Icon.vue'

const props = defineProps<{
  dirty: boolean
  dirtyLabel?: string
  saving?: boolean
}>()

const emit = defineEmits<{ revert: []; apply: [] }>()
</script>

<template>
  <footer class="foot">
    <template v-if="props.dirty">
      <span class="bdg warn"><span class="dot" />{{ props.dirtyLabel ?? 'Unsaved changes' }}</span>
    </template>
    <template v-else>
      <span class="muted" style="display: inline-flex; align-items: center; gap: 5px">
        <Icon name="check" class="ok-t" />All changes applied
      </span>
    </template>
    <span class="sp" />
    <button type="button" class="btn ghost" :disabled="!props.dirty || props.saving" @click="emit('revert')">
      Revert
    </button>
    <button type="button" class="btn pri" :disabled="!props.dirty || props.saving" @click="emit('apply')">
      {{ props.saving ? 'Applying…' : 'Apply' }}
    </button>
  </footer>
</template>
