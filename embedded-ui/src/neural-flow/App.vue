<script setup lang="ts">
/**
 * Hazor Neural Flow.
 *
 * Holds the pieces together and nothing more: it asks the backend for the node
 * contract and the registered models, hands the catalog to the node registry,
 * lets the user build a graph, and sends that graph to the workflow controller
 * that already exists in C++.
 *
 * What it deliberately does not do: validate a workflow, resolve a model path,
 * run inference, or decide where a generated image goes. Each of those has
 * exactly one implementation, and it is not in this page.
 */
import { computed, onBeforeUnmount, ref, shallowRef } from 'vue'

import { QtBridgeError, isHostedByQt } from '@shared/bridge/qtBridge'
import { applyThemeVariables } from '@shared/design-system/applyTheme'

import FlowHeader from './components/FlowHeader.vue'
import NeuralGraph from './components/NeuralGraph.vue'
import NodePalette from './components/NodePalette.vue'
import type { LGraph, LGraphCanvas } from './graph/litegraph'
import {
  buildStartingGraph,
  clearErrorHighlights,
  highlightErrorNodes,
  nodeForDocumentId,
  toWorkflowDocument,
} from './graph/workflow'
import { setModels } from './nodes/modelStore'
import { createNode, registerCatalogNodes } from './nodes/registerNodes'
import type {
  CompletedInfo,
  GenerationState,
  ModelEntry,
  NodeCatalog,
  NodeDefinition,
  ThemeVariables,
  WorkflowError,
} from './services/contract'
import { NeuralFlowClient } from './services/neuralFlow'
import { statusKindFor, statusLabelFor } from './services/status'

const client = shallowRef<NeuralFlowClient | null>(null)
const catalog = shallowRef<NodeCatalog | null>(null)
const theme = ref<ThemeVariables>({})
const models = ref<ModelEntry[]>([])
const defaultModelId = ref('')
const backend = ref('')
const modelConfigPath = ref('')
const hasDocument = ref(false)
const connectionError = ref('')

const runState = ref<GenerationState>('idle')
const runDetail = ref('')
const progress = ref<{ step: number; steps: number } | null>(null)
const lastResult = ref<CompletedInfo | null>(null)
const runErrors = ref<WorkflowError[]>([])
const graphReady = ref(false)

// Not reactive: nothing in the template reads the graph itself, and wrapping
// litegraph's object graph in a proxy would mean Vue walking thousands of
// nodes, links and widgets on every change.
let graph: LGraph | null = null
let canvas: LGraphCanvas | null = null

const subscriptions: Array<() => void> = []

const busy = computed(() => runState.value === 'loading' || runState.value === 'generating')
const stateKind = computed(() => statusKindFor(runState.value))
const stateLabel = computed(() => statusLabelFor(runState.value))

const canRun = computed(
  () => !busy.value && graphReady.value && client.value !== null && hasDocument.value,
)

const checkpointCount = computed(
  () =>
    models.value.filter(
      (model) => model.category === 'checkpoint' || model.category === 'diffusion_model',
    ).length,
)

const detail = computed(() => {
  if (runErrors.value.length > 0) {
    const first = runErrors.value[0]
    const extra = runErrors.value.length - 1
    return extra > 0 ? `${first?.message} (+${extra})` : (first?.message ?? '')
  }
  if (runState.value === 'generating' && progress.value) {
    return `step ${progress.value.step} of ${progress.value.steps}`
  }
  if (runState.value === 'completed' && lastResult.value) {
    return describeResult(lastResult.value)
  }
  return runDetail.value
})

/**
 * What the page says when it cannot do its job. These are the two states a
 * silent empty canvas would hide: no document to insert a layer into, and no
 * model to load.
 */
const notices = computed(() => {
  const list: string[] = []
  if (connectionError.value) {
    list.push(connectionError.value)
  }
  if (client.value && !hasDocument.value) {
    list.push(
      'Open or create a document first: a generated image is inserted as a new pixel layer in the active one.',
    )
  }
  if (catalog.value && checkpointCount.value === 0) {
    list.push(
      modelConfigPath.value
        ? `No Stable Diffusion model is registered. Add one to ${modelConfigPath.value} (or point HAZOR_SD_MODEL at a checkpoint), then use Refresh models.`
        : 'No Stable Diffusion model is registered.',
    )
  }
  return list
})

function describeResult(result: CompletedInfo): string {
  const seconds = (milliseconds: number) => (milliseconds / 1000).toFixed(1)
  // The device that ran it, not the one that was asked for: a CPU run on a GPU
  // build has to read as a CPU run.
  const device = result.cpuFallback ? `${result.device} (GPU unavailable)` : result.device
  return (
    `${result.width}×${result.height} on ${device} — seed ${result.seed}, ` +
    `load ${seconds(result.loadMs)} s, generate ${seconds(result.generateMs)} s ` +
    `→ layer “${result.layerName}”`
  )
}

function describe(error: unknown): string {
  return error instanceof Error ? error.message : String(error)
}

/** The node title behind an error's node id, when the error names one. */
function nodeLabelFor(nodeId: string): string {
  if (!graph || !nodeId) {
    return ''
  }
  return nodeForDocumentId(graph, nodeId)?.title ?? ''
}

function showErrors(errors: WorkflowError[]): void {
  runErrors.value = errors
  progress.value = null
  runState.value = 'failed'
  if (graph) {
    const colour = theme.value.danger
    highlightErrorNodes(graph, errors, typeof colour === 'string' ? colour : '#d05a5a')
    canvas?.setDirty(true, true)
  }
}

function clearRunFeedback(): void {
  runErrors.value = []
  lastResult.value = null
  progress.value = null
  if (graph) {
    clearErrorHighlights(graph)
    canvas?.setDirty(true, true)
  }
}

function subscribe(connected: NeuralFlowClient): void {
  subscriptions.push(
    connected.on('workflowStarted', () => {
      clearRunFeedback()
    }),
    connected.on('workflowStateChanged', (state, stateDetail) => {
      runState.value = state
      runDetail.value = stateDetail
    }),
    connected.on('workflowProgress', (step, steps) => {
      progress.value = { step, steps }
    }),
    connected.on('workflowCompleted', (result) => {
      lastResult.value = result
      progress.value = null
    }),
    connected.on('workflowFailed', (errors) => {
      showErrors(errors ?? [])
    }),
    connected.on('modelsChanged', (next) => {
      models.value = next ?? []
      setModels(models.value)
      // The model combos read their options through a function, so nothing has
      // to be rebuilt — only redrawn.
      canvas?.setDirty(true, true)
    }),
    connected.on('documentAvailabilityChanged', (available) => {
      hasDocument.value = available
    }),
    connected.on('themeChanged', (next) => {
      theme.value = next
      applyThemeVariables(next)
      if (graph) {
        clearErrorHighlights(graph)
      }
    }),
  )
}

async function connect(): Promise<void> {
  connectionError.value = ''
  try {
    if (!isHostedByQt()) {
      throw new QtBridgeError(
        'This interface runs inside Hazor. Open it from the Stable Diffusion panel.',
      )
    }
    const connected = await NeuralFlowClient.connect()
    const state = await connected.initialState()

    catalog.value = state.nodeDefinitions
    // Before anything can place a node, and before the graph component mounts:
    // the template only renders it once the catalog is set.
    registerCatalogNodes(state.nodeDefinitions)

    models.value = state.models ?? []
    setModels(models.value)
    defaultModelId.value = state.defaultModelId ?? ''
    theme.value = state.theme ?? {}
    applyThemeVariables(state.theme)
    backend.value = state.backend ?? ''
    modelConfigPath.value = state.modelConfigPath ?? ''
    hasDocument.value = Boolean(state.hasDocument)
    // A run already in flight when the panel is opened: the state signals will
    // carry it from here.
    runState.value = state.busy ? 'generating' : 'idle'

    client.value = connected
    subscribe(connected)
  } catch (error) {
    connectionError.value = describe(error)
  }
}

function onGraphReady(ready: LGraph, readyCanvas: LGraphCanvas): void {
  graph = ready
  canvas = readyCanvas
  const activeCatalog = catalog.value
  if (activeCatalog) {
    buildStartingGraph(ready, activeCatalog, defaultModelId.value)
  }
  graphReady.value = true
}

function addNode(definition: NodeDefinition): void {
  if (!graph || !canvas) {
    return
  }
  const node = createNode(graph, definition)
  if (!node) {
    return
  }
  // Dropped into the middle of what is on screen, in graph coordinates, so it
  // lands in view whatever the pan and zoom are.
  const area = canvas.ds.visible_area
  if (area && area[2] > 0 && area[3] > 0) {
    node.pos = [area[0] + area[2] * 0.35, area[1] + area[3] * 0.35]
  }
  canvas.setDirty(true, true)
}

async function run(): Promise<void> {
  const activeClient = client.value
  const activeCatalog = catalog.value
  if (!activeClient || !activeCatalog || !graph) {
    return
  }
  clearRunFeedback()

  // The graph is read once, here, and converted explicitly: what the backend
  // receives is a document in its own schema, never LiteGraph's serialisation.
  const workflow = toWorkflowDocument(graph, activeCatalog)
  try {
    const response = await activeClient.executeWorkflow(workflow)
    if (!response.started) {
      showErrors(response.errors ?? [])
    }
  } catch (error) {
    const message = describe(error)
    showErrors([{ code: 'bridge', nodeId: '', portId: '', message }])
    activeClient.reportError('executeWorkflow', message)
  }
}

function cancel(): void {
  client.value?.cancelWorkflow()
}

async function refreshModels(): Promise<void> {
  const activeClient = client.value
  if (!activeClient) {
    return
  }
  try {
    const next = await activeClient.refreshModels()
    models.value = next ?? []
    setModels(models.value)
    canvas?.setDirty(true, true)
  } catch (error) {
    const message = describe(error)
    connectionError.value = message
    activeClient.reportError('refreshModels', message)
  }
}

onBeforeUnmount(() => {
  for (const unsubscribe of subscriptions) {
    unsubscribe()
  }
  subscriptions.length = 0
})

void connect()
</script>

<template>
  <div class="neural-flow">
    <FlowHeader
      :state-label="stateLabel"
      :state-kind="stateKind"
      :detail="detail"
      :busy="busy"
      :can-run="canRun"
      :backend="backend"
      @run="run"
      @cancel="cancel"
      @refresh-models="refreshModels"
    />

    <div v-if="notices.length > 0" class="neural-flow__notices">
      <p v-for="notice in notices" :key="notice" class="neural-flow__notice">{{ notice }}</p>
    </div>

    <div class="neural-flow__body">
      <template v-if="catalog">
        <NodePalette
          :categories="catalog.categories"
          :nodes="catalog.nodes"
          @add="addNode"
        />
        <NeuralGraph
          :theme="theme"
          :port-types="catalog.portTypes"
          @ready="onGraphReady"
        />
      </template>
      <p v-else class="neural-flow__placeholder">
        {{ connectionError ? 'The node catalog could not be loaded.' : 'Connecting to Hazor…' }}
      </p>
    </div>

    <ul v-if="runErrors.length > 0" class="neural-flow__errors">
      <li v-for="(error, index) in runErrors.slice(0, 4)" :key="`${error.code}-${index}`">
        <span v-if="nodeLabelFor(error.nodeId)" class="neural-flow__errors-node">
          {{ nodeLabelFor(error.nodeId) }}
        </span>
        {{ error.message }}
      </li>
    </ul>
  </div>
</template>

<style scoped>
.neural-flow {
  display: flex;
  flex-direction: column;
  height: 100%;
}

.neural-flow__notices {
  flex: none;
  border-bottom: 1px solid var(--hz-border);
  background: var(--hz-surface-dark);
}

.neural-flow__notice {
  margin: 0;
  padding: var(--hz-space-1) var(--hz-space-3);
  color: var(--hz-warning);
  font-size: var(--hz-font-size-sm);
}

.neural-flow__body {
  flex: 1;
  display: grid;
  grid-template-columns: 170px minmax(0, 1fr);
  /* Without this the grid refuses to shrink below its content and the canvas
     grows the panel instead of fitting it. */
  min-height: 0;
}

.neural-flow__placeholder {
  grid-column: 1 / -1;
  margin: 0;
  display: grid;
  place-items: center;
  color: var(--hz-text-secondary);
}

.neural-flow__errors {
  flex: none;
  margin: 0;
  padding: var(--hz-space-2) var(--hz-space-3) var(--hz-space-2) var(--hz-space-4);
  max-height: 25%;
  overflow-y: auto;
  list-style: disc;
  background: var(--hz-surface-dark);
  border-top: 1px solid var(--hz-border);
  color: var(--hz-danger);
  font-size: var(--hz-font-size-sm);
}

.neural-flow__errors-node {
  color: var(--hz-text-bright);
  font-weight: 600;
}
</style>
