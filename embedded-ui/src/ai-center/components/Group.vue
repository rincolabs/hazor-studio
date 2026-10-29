<script setup lang="ts">
import { ref } from 'vue'

import Icon from './Icon.vue'

const props = defineProps<{
  title: string
  /** Groups are static sections by default; only a few (e.g. "Test Prompt") collapse. */
  collapsible?: boolean
  defaultOpen?: boolean
}>()

const open = ref(props.collapsible ? Boolean(props.defaultOpen) : true)

function toggle(): void {
  if (props.collapsible) {
    open.value = !open.value
  }
}
</script>

<template>
  <section class="grp" :class="{ closed: props.collapsible && !open }" :aria-label="props.title">
    <button v-if="props.collapsible" type="button" class="grp-h" :aria-expanded="open" @click="toggle">
      <Icon :name="open ? 'chevronDown' : 'chevronRight'" />
      <slot name="icon" />
      {{ props.title }}
      <span v-if="$slots.meta" class="meta"><slot name="meta" /></span>
    </button>
    <div v-else class="grp-h">
      <Icon name="chevronDown" />
      <slot name="icon" />
      {{ props.title }}
      <span v-if="$slots.meta" class="meta"><slot name="meta" /></span>
    </div>
    <div v-if="open" class="grp-b">
      <slot />
    </div>
  </section>
</template>
