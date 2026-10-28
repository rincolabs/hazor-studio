<script setup lang="ts">
/**
 * The node list down the left side, grouped by the catalog's own categories.
 *
 * It adds nodes; it does not know how. The parent places what comes back,
 * which is also how the LiteGraph context menu adds them — both end up calling
 * the same registration.
 */
import { computed } from 'vue'

import type { NodeCategory, NodeDefinition } from '../services/contract'

const props = defineProps<{
  categories: NodeCategory[]
  nodes: NodeDefinition[]
}>()

const emit = defineEmits<{
  add: [definition: NodeDefinition]
}>()

/** Only categories that actually have node types in this build are shown. */
const groups = computed(() =>
  props.categories
    .map((category) => ({
      ...category,
      nodes: props.nodes.filter((node) => node.category === category.id),
    }))
    .filter((group) => group.nodes.length > 0),
)
</script>

<template>
  <aside class="palette" aria-label="Nodes">
    <h2 class="palette__heading">Nodes</h2>
    <div v-for="group in groups" :key="group.id" class="palette__group">
      <p class="palette__category">{{ group.label }}</p>
      <button
        v-for="definition in group.nodes"
        :key="definition.type"
        type="button"
        class="palette__node"
        :title="`Add ${definition.label}`"
        @click="emit('add', definition)"
      >
        {{ definition.label }}
      </button>
    </div>
  </aside>
</template>

<style scoped>
.palette {
  display: flex;
  flex-direction: column;
  gap: var(--hz-space-2);
  padding: var(--hz-space-2);
  background: var(--hz-surface);
  border-right: 1px solid var(--hz-border);
  overflow-y: auto;
  min-height: 0;
}

.palette__heading {
  margin: 0 0 var(--hz-space-1) var(--hz-space-1);
  color: var(--hz-text-secondary);
  font-size: var(--hz-font-size-sm);
  font-weight: 600;
  letter-spacing: 0.08em;
  text-transform: uppercase;
}

.palette__group {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.palette__category {
  margin: var(--hz-space-2) 0 var(--hz-space-1) var(--hz-space-1);
  color: var(--hz-text-disabled);
  font-size: var(--hz-font-size-sm);
}

.palette__node {
  display: block;
  width: 100%;
  height: auto;
  padding: var(--hz-space-1) var(--hz-space-2);
  text-align: left;
  background: transparent;
  border: 1px solid transparent;
  border-radius: var(--hz-radius-sm);
  color: var(--hz-text);
}

.palette__node:hover {
  background: var(--hz-surface-hover);
  border-color: var(--hz-border);
}
</style>
