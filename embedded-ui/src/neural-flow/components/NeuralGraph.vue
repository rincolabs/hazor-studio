<script setup lang="ts">
/**
 * The LiteGraph editor, wrapped as a Vue component.
 *
 * It owns the graph and the canvas for the lifetime of the component, plus
 * the menu wiring (Shift+A, right-click, combo popups) that has to live next
 * to the canvas element itself. Workflow conversion and the bridge stay with
 * the parent, which gets both litegraph objects through `ready`.
 *
 * The surface has no size of its own — the panel is resized by Hazor's dock
 * system — so it follows the element's box through a ResizeObserver and is
 * allocated at the screen's pixel density rather than at a fixed resolution.
 */
import { onBeforeUnmount, onMounted, ref, shallowRef } from 'vue'

import { closeMenu, openMenu } from '../composables/useMenu'
import { attachCanvasMenuBridge, type CanvasMenuBridge } from '../graph/canvasMenuBridge'
import {
  type LGraph,
  type LGraphCanvas,
  createGraphCanvas,
  releaseGraphCanvas,
  resizeGraphCanvas,
} from '../graph/litegraph'
import { widgetHooks } from '../graph/canvasWidgets'
import { installTextareaOverlay } from '../nodes/textareaWidget'
import { litegraphTypeFor } from '../nodes/registerNodes'
import { openAddNodeMenu, openWorkspaceContextMenu } from './menu/catalogMenu'
import type { NodeCatalog, NodeDefinition } from '../services/contract'

const props = defineProps<{
  catalog: NodeCatalog
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
let menuBridge: CanvasMenuBridge | null = null
let pixelRatio = 1

function pickByDefinition(definition: NodeDefinition, pick: (type: string) => void): void {
  pick(litegraphTypeFor(definition))
}

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

  widgetHooks.openMenu = openMenu
  menuBridge = attachCanvasMenuBridge({
    canvas: created.canvas,
    openAddMenu: (x, y, pick) =>
      openAddNodeMenu(props.catalog, x, y, (definition) => pickByDefinition(definition, pick)),
    openCanvasMenu: (x, y, pick) =>
      openWorkspaceContextMenu(props.catalog, x, y, (definition) => pickByDefinition(definition, pick)),
  })

  observer = new ResizeObserver(() => {
    pixelRatio = resizeGraphCanvas(created.canvas, element, pixelRatio)
  })
  observer.observe(container)
  // Once up front: the observer fires on changes, and the first layout is not
  // one.
  pixelRatio = resizeGraphCanvas(created.canvas, element, pixelRatio)

  emit('ready', created.graph, created.canvas)
})

onBeforeUnmount(() => {
  closeMenu()
  widgetHooks.openMenu = null
  menuBridge?.detach()
  menuBridge = null
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

/** Opens the Add Node menu anchored at a client position, e.g. under the toolbar's Add button. */
function openAddAt(clientX: number, clientY: number): void {
  if (!menuBridge) return
  openAddNodeMenu(props.catalog, clientX, clientY, (definition) =>
    pickByDefinition(definition, menuBridge!.pickType),
  )
}

defineExpose({ openAddAt })
</script>

<template>
  <div ref="host" class="neural-graph">
    <!-- tabindex so litegraph's keyboard shortcuts (delete, copy, paste) reach it. -->
    <canvas ref="surface" class="neural-graph__surface" tabindex="0"></canvas>
    <div class="neural-graph__hint">
      <span class="kbd">Shift</span><span class="kbd">A</span><span>Add node</span>
      <span class="sep" />
      <span class="kbd">Right-click</span><span>Context menu</span>
      <span class="sep" />
      <span>Wheel zoom · drag background to pan</span>
    </div>
  </div>
</template>

<style scoped>
.neural-graph {
  position: relative;
  flex: 1 1 auto;
  width: 100%;
  min-width: 0;
  min-height: 0;
  overflow: hidden;
  background: var(--ws-bg);
}

.neural-graph__surface {
  display: block;
  width: 100%;
  height: 100%;
  outline: none;
}

.neural-graph__hint {
  position: absolute;
  left: 10px;
  bottom: 10px;
  display: flex;
  align-items: center;
  gap: 5px;
  height: 26px;
  padding: 0 10px;
  border: 1px solid var(--line-soft);
  border-radius: 13px;
  background: rgba(37, 39, 40, 0.88);
  color: var(--text-3);
  font-size: var(--fs-sm);
  white-space: nowrap;
  pointer-events: none;
}

.neural-graph__hint .sep {
  width: 1px;
  height: 12px;
  margin: 0 4px;
  background: var(--line);
}

@media (max-width: 760px) {
  .neural-graph__hint {
    display: none;
  }
}

/* The frame is the wrapper's, so the footer sits inside it — see the design's
   `.nf-ta` in tmp/neural-flow-ui/src/styles/nodes.css. */
:global(.hazor-node-textarea) {
  position: absolute;
  top: 0;
  left: 0;
  z-index: 1;
  display: flex;
  flex-direction: column;
  box-sizing: border-box;
  overflow: hidden;
  transform-origin: 0 0;
  border: 1px solid var(--widget-border);
  border-radius: 3px;
  background: var(--widget-bg);
  color: var(--text);
  font-family: var(--font);
}

:global(.hazor-node-textarea:hover) {
  border-color: var(--widget-border-hover);
}

:global(.hazor-node-textarea:focus-within) {
  border-color: var(--accent);
}

:global(.hazor-node-textarea__input) {
  flex: 1;
  min-width: 0;
  min-height: 0;
  box-sizing: border-box;
  resize: none;
  padding: 6px 10px 2px;
  border: 0;
  outline: none;
  background: transparent;
  color: var(--text);
  font: inherit;
  font-size: 11.5px;
  line-height: 1.45;
  user-select: text;
}

:global(.hazor-node-textarea__input::placeholder) {
  color: var(--text-disabled);
}

:global(.hazor-node-textarea__foot) {
  flex: none;
  display: flex;
  justify-content: space-between;
  gap: 8px;
  padding: 0 10px 4px;
  overflow: hidden;
  color: var(--text-3);
  font-size: 10px;
  white-space: nowrap;
}
</style>
