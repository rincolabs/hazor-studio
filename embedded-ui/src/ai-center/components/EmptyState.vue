<script setup lang="ts">
import Icon from './Icon.vue'
import type { IconName } from './icons'

defineProps<{
  icon: IconName
  title: string
}>()
</script>

<template>
  <div class="empty">
    <Icon :name="icon" size="xl" />
    <div class="empty-t">{{ title }}</div>
    <slot />
  </div>
</template>
