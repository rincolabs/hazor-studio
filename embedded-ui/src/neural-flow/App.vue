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

import Icon from './components/ui/Icon.vue'
import FlowToolbar from './components/layout/FlowToolbar.vue'
import MenuHost from './components/menu/MenuHost.vue'
import NeuralGraph from './components/NeuralGraph.vue'
import type { LGraph, LGraphCanvas } from './graph/litegraph'
import {
  buildStartingGraph,
  clearErrorHighlights,
  highlightErrorNodes,
  nodeForDocumentId,
  showRunState,
  toWorkflowDocument,
} from './graph/workflow'
import { setModels } from './nodes/modelStore'
import { registerCatalogNodes } from './nodes/registerNodes'
import type {
  CompletedInfo,
  GenerationState,
  ModelEntry,
  NodeCatalog,
  WorkflowError,
} from './services/contract'
import { NeuralFlowClient } from './services/neuralFlow'
import { statusDetailFor, statusKindFor, statusLabelFor } from './services/status'

const client = shallowRef<NeuralFlowClient | null>(null)
const catalog = shallowRef<NodeCatalog | null>(null)
const models = ref<ModelEntry[]>([])
const defaultModelId = ref('')
const modelConfigPath = ref('')
const hasDocument = ref(false)
const connectionError = ref('')
const refreshing = ref(false)
const graphComponent = ref<InstanceType<typeof NeuralGraph> | null>(null)

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
const stateLabel = computed(() => statusLabelFor(runState.value, progress.value))
const progressFraction = computed(() =>
  progress.value && progress.value.steps > 0 ? progress.value.step / progress.value.steps : null,
)

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
  if (runState.value === 'completed' && lastResult.value) {
    return describeResult(lastResult.value)
  }
  return statusDetailFor(runState.value, progress.value, runDetail.value)
})

/**
 * What the page says when it cannot do its job. These are the two states a
 * silent empty canvas would hide: no document to insert a layer into, and no
 * model to load.
 */
const notices = computed(() => {
  const list: string[] = []
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
    highlightErrorNodes(graph, errors)
    canvas?.setDirty(true, true)
  }
}

/** Mirrors the run onto the graph: badge, progress bar and flowing links. */
function syncNodeRunState(): void {
  if (graph) {
    showRunState(graph, runState.value, progress.value)
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
      // Loading reports model tensors and generating reports denoising steps.
      // Never let the first bar's final count masquerade as the next phase.
      if (state !== runState.value) progress.value = null
      runState.value = state
      runDetail.value = stateDetail
      syncNodeRunState()
    }),
    connected.on('workflowProgress', (step, steps) => {
      progress.value = { step, steps }
      syncNodeRunState()
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

function openAddMenu(button: HTMLElement): void {
  const rect = button.getBoundingClientRect()
  graphComponent.value?.openAddAt(rect.right - 220, rect.bottom + 4)
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
  refreshing.value = true
  try {
    const next = await activeClient.refreshModels()
    models.value = next ?? []
    setModels(models.value)
    canvas?.setDirty(true, true)
  } catch (error) {
    const message = describe(error)
    connectionError.value = message
    activeClient.reportError('refreshModels', message)
  } finally {
    refreshing.value = false
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
    <FlowToolbar
      :state-label="stateLabel"
      :state-kind="stateKind"
      :detail="detail"
      :progress="progressFraction"
      :busy="busy"
      :can-run="canRun"
      :refreshing="refreshing"
      @run="run"
      @cancel="cancel"
      @refresh-models="refreshModels"
      @add="openAddMenu"
    />

    <div v-if="connectionError || notices.length > 0" class="neural-flow__banners">
      <div v-if="connectionError" class="bn err">
        <Icon name="warning" :size="14" />
        <span>{{ connectionError }}</span>
      </div>
      <div v-for="notice in notices" :key="notice" class="bn warn">
        <Icon name="warning" :size="14" />
        <span>{{ notice }}</span>
      </div>
    </div>

    <main class="neural-flow__stage">
      <NeuralGraph v-if="catalog" ref="graphComponent" :catalog="catalog" @ready="onGraphReady" />
      <p v-else class="neural-flow__placeholder">
        {{ connectionError ? 'The node catalog could not be loaded.' : 'Connecting to Hazor…' }}
      </p>
    </main>

    <ul v-if="runErrors.length > 0" class="neural-flow__errors">
      <li v-for="(error, index) in runErrors.slice(0, 4)" :key="`${error.code}-${index}`">
        <span v-if="nodeLabelFor(error.nodeId)" class="neural-flow__errors-node">
          {{ nodeLabelFor(error.nodeId) }}
        </span>
        {{ error.message }}
      </li>
    </ul>

    <MenuHost />
  </div>
</template>

<style scoped>
.neural-flow {
  display: flex;
  flex-direction: column;
  width: 100%;
  height: 100%;
  overflow: hidden;
}

.neural-flow__banners {
  flex: none;
  display: flex;
  flex-direction: column;
  gap: 1px;
}

.neural-flow__banners .bn {
  border-radius: 0;
  border-left: 0;
  border-right: 0;
}

.neural-flow__stage {
  position: relative;
  flex: 1 1 auto;
  min-height: 0;
  display: flex;
  overflow: hidden;
}

.neural-flow__placeholder {
  flex: 1;
  margin: 0;
  display: grid;
  place-items: center;
  color: var(--text-3);
}

.neural-flow__errors {
  flex: none;
  margin: 0;
  padding: 8px 12px 8px 28px;
  max-height: 25%;
  overflow-y: auto;
  list-style: disc;
  background: var(--err-bg);
  border-top: 1px solid var(--line-soft);
  color: var(--err);
  font-size: var(--fs-sm);
}

.neural-flow__errors-node {
  color: var(--text);
  font-weight: 600;
}
</style>
