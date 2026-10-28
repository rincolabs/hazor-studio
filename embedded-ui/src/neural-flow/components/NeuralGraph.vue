<script setup lang="ts">
/**
 * The LiteGraph editor, wrapped as a Vue component.
 *
 * It owns the graph and the canvas for the lifetime of the component and
 * nothing else: no workflow conversion, no bridge, no node catalog. The parent
 * gets both objects through `ready` and drives them.
 *
 * The surface has no size of its own — the panel is resized by Hazor's dock
 * system — so it follows the element's box through a ResizeObserver and is
 * allocated at the screen's pixel density rather than at a fixed resolution.
 */
import { onBeforeUnmount, onMounted, ref, shallowRef, watch } from 'vue'

import {
  type LGraph,
  type LGraphCanvas,
  applyGraphTheme,
  createGraphCanvas,
  releaseGraphCanvas,
  resizeGraphCanvas,
} from '../graph/litegraph'
import { installTextareaOverlay } from '../nodes/textareaWidget'
import type { ThemeVariables } from '../services/contract'

const props = defineProps<{
  theme: ThemeVariables
  portTypes: string[]
}>()

const emit = defineEmits<{
  ready: [graph: LGraph, canvas: LGraphCanvas]
}>()

const host = ref<HTMLDivElement | null>(null)
const surface = ref<HTMLCanvasElement | null>(null)
const graph = shallowRef<LGraph | null>(null)
const canvas = shallowRef<LGraphCanvas | null>(null)

let observer: ResizeObserver | null = null
let releaseTextareaOverlay: (() => void) | null = null
let pixelRatio = 1

onMounted(() => {
  const element = surface.value
  const container = host.value
  if (!element || !container) {
    return
  }

  const created = createGraphCanvas(element)
  graph.value = created.graph
  canvas.value = created.canvas
  releaseTextareaOverlay = installTextareaOverlay(created.graph, created.canvas, container, element)
  applyGraphTheme(created.canvas, props.theme, props.portTypes)

  observer = new ResizeObserver(() => {
    pixelRatio = resizeGraphCanvas(created.canvas, element, pixelRatio)
  })
  observer.observe(container)
  // Once up front: the observer fires on changes, and the first layout is not
  // one.
  pixelRatio = resizeGraphCanvas(created.canvas, element, pixelRatio)

  emit('ready', created.graph, created.canvas)
})

watch(
  () => props.theme,
  (theme) => {
    if (canvas.value) {
      applyGraphTheme(canvas.value, theme, props.portTypes)
    }
  },
)

onBeforeUnmount(() => {
  observer?.disconnect()
  observer = null
  releaseTextareaOverlay?.()
  releaseTextareaOverlay = null
  if (canvas.value && graph.value) {
    releaseGraphCanvas(canvas.value, graph.value)
  }
  canvas.value = null
  graph.value = null
})
</script>

<template>
  <div ref="host" class="neural-graph">
    <!-- tabindex so litegraph's keyboard shortcuts (delete, copy, paste) reach it. -->
    <canvas ref="surface" class="neural-graph__surface" tabindex="0"></canvas>
  </div>
</template>

<style scoped>
.neural-graph {
  position: relative;
  min-width: 0;
  min-height: 0;
  overflow: hidden;
  background: var(--hz-background);
}

.neural-graph__surface {
  display: block;
  width: 100%;
  height: 100%;
  outline: none;
}

:global(.hazor-node-textarea) {
  position: absolute;
  top: 0;
  left: 0;
  z-index: 1;
  display: flex;
  flex-direction: column;
  gap: var(--hz-space-1);
  box-sizing: border-box;
  padding: var(--hz-space-1);
  overflow: hidden;
  transform-origin: 0 0;
  border: 1px solid var(--hz-border);
  border-radius: var(--hz-radius-sm);
  background: var(--hz-surface-dark);
  color: var(--hz-text);
  font-family: var(--hz-font-family);
}

:global(.hazor-node-textarea__label) {
  flex: none;
  overflow: hidden;
  color: var(--hz-text-secondary);
  font-size: var(--hz-font-size-sm);
  line-height: 14px;
  text-overflow: ellipsis;
  white-space: nowrap;
}

:global(.hazor-node-textarea__input) {
  flex: 1;
  min-width: 0;
  min-height: 0;
  box-sizing: border-box;
  resize: none;
  padding: var(--hz-space-1);
  border: 1px solid var(--hz-border);
  border-radius: var(--hz-radius-sm);
  outline: none;
  background: var(--hz-surface);
  color: var(--hz-text-bright);
  font: inherit;
  font-size: var(--hz-font-size-sm);
  line-height: 1.35;
}

:global(.hazor-node-textarea__input:focus) {
  border-color: var(--hz-border-focus);
}
</style>
