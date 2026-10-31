<script setup lang="ts">
import { ICONS, type IconName } from './icons'

defineProps<{
  name: IconName
  size?: number
}>()
</script>

<template>
  <svg
    class="i"
    :width="size ?? 14"
    :height="size ?? 14"
    :style="{ width: `${size ?? 14}px`, height: `${size ?? 14}px` }"
    viewBox="0 0 24 24"
    aria-hidden="true"
    v-html="ICONS[name]"
  />
</template>
