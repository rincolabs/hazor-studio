<script setup lang="ts">
import { onMounted, ref } from 'vue'
import Icon from '../ui/Icon.vue'

const model = defineModel<string>({ default: '' })
withDefaults(defineProps<{ placeholder?: string }>(), { placeholder: 'Search…' })

const input = ref<HTMLInputElement | null>(null)
onMounted(() => requestAnimationFrame(() => input.value?.focus()))
</script>

<template>
  <label class="mn-search">
    <Icon name="search" :size="13" />
    <input
      ref="input"
      v-model="model"
      type="text"
      :placeholder="placeholder"
      spellcheck="false"
      autocomplete="off"
      aria-label="Search nodes"
    >
    <button v-if="model" class="mn-clear" type="button" aria-label="Clear search" @mousedown.prevent @click="model = ''">
      <Icon name="close" :size="11" />
    </button>
  </label>
</template>

<style scoped>
.mn-search {
  display: flex;
  align-items: center;
  gap: 6px;
  height: 26px;
  margin: 2px 4px 4px;
  padding: 0 6px 0 8px;
  background: var(--input);
  border: 1px solid var(--line);
  border-radius: var(--r-field);
  color: var(--text-3);
}
.mn-search:focus-within {
  border-color: var(--accent);
  box-shadow: 0 0 0 1px var(--accent);
}
input {
  flex: 1;
  min-width: 0;
  border: 0;
  outline: none;
  background: transparent;
  color: var(--text);
  padding: 0;
  user-select: text;
}
input::placeholder {
  color: var(--text-disabled);
}
.mn-clear {
  width: 16px;
  height: 16px;
  padding: 0;
  border: 0;
  border-radius: 2px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  background: transparent;
  color: var(--text-3);
  cursor: pointer;
}
.mn-clear:hover {
  background: rgba(255, 255, 255, 0.08);
  color: var(--text);
}
</style>
