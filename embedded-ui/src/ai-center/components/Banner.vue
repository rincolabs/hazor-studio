<script setup lang="ts">
import { computed } from 'vue'

import Icon from './Icon.vue'
import type { IconName } from './icons'

const props = defineProps<{
  severity: 'info' | 'warn' | 'err' | 'ok'
  title?: string
}>()

const icon = computed<IconName>(() => {
  switch (props.severity) {
    case 'warn':
      return 'warning'
    case 'err':
      return 'error'
    case 'ok':
      return 'check'
    default:
      return 'info'
  }
})
</script>

<template>
  <div class="bn" :class="props.severity">
    <Icon :name="icon" />
    <div>
      <div v-if="props.title" class="bn-t">{{ props.title }}</div>
      <slot />
    </div>
    <slot name="action" />
  </div>
</template>
