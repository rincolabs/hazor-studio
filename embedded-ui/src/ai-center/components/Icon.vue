<script setup lang="ts">
import { ICONS, type IconName } from './icons'

const props = defineProps<{
  name: IconName
  size?: 'sm' | 'lg' | 'xl'
}>()
</script>

<template>
  <svg
    class="i"
    :class="props.size === 'lg' ? 'lg' : props.size === 'xl' ? 'xl' : undefined"
    viewBox="0 0 24 24"
    aria-hidden="true"
    v-html="ICONS[props.name]"
  />
</template>
