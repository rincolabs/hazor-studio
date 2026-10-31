/**
 * The conversion between what LiteGraph holds and what Hazor executes.
 *
 * These are two different formats and are deliberately not assumed to be
 * compatible. LiteGraph's graph carries positions, link ids, widget values and
 * slot indices; the executable document carries node ids, named ports and
 * validated property values. Nothing here decides whether a graph is runnable:
 * `sdlocal::SdWorkflowValidator` does, in C++, and it is the only validator.
 */
import type { LGraph, LGraphNode } from './litegraph'
import { LiteGraph, graphNodes } from './litegraph'
import { isHazorVisualNode, setNodeState } from './nodeVisualState'
import { toWireValue } from '../nodes/propertyWidgets'
import {
  type HazorNode,
  createNode,
  inputPortForSlot,
  inputSlotForPort,
} from '../nodes/registerNodes'
import type {
  GenerationState,
  NodeCatalog,
  NodeDefinition,
  WorkflowConnection,
  WorkflowDocument,
  WorkflowNode,
} from '../services/contract'
import { nodeRunLabelFor } from '../services/status'

/** Space between the columns and rows of the starting graph. */
const COLUMN_GAP = 90
const ROW_GAP = 28
const ORIGIN: [number, number] = [50, 50]

/**
 * The node ids a document carries. LiteGraph numbers its nodes; the backend
 * wants strings, and keeping the number inside the string is what lets an
 * error that names a node be pointed back at one.
 */
export function documentIdFor(node: LGraphNode): string {
  return `n${node.id}`
}

export function nodeForDocumentId(graph: LGraph, documentId: string): LGraphNode | null {
  const match = /^n(\d+)$/.exec(documentId)
  if (!match) {
    return null
  }
  return graph.getNodeById(Number(match[1])) ?? null
}

/**
 * Every property the type declares, in wire space.
 *
 * All of them, whether the user touched them or not: a document that spells
 * out what will run is one a user can read back, and it does not depend on the
 * executor and the editor agreeing about an absent key — even though they do,
 * since both take their defaults from the same catalog.
 */
function wireProperties(definition: NodeDefinition, node: LGraphNode): Record<string, unknown> {
  const properties: Record<string, unknown> = {}
  for (const spec of definition.properties) {
    properties[spec.key] = toWireValue(spec, node.properties?.[spec.key] ?? spec.default)
  }
  return properties
}

/** Reads the current graph as a document the workflow controller can run. */
export function toWorkflowDocument(graph: LGraph, catalog: NodeCatalog): WorkflowDocument {
  const definitions = new Map(catalog.nodes.map((definition) => [definition.type, definition]))
  const nodes: WorkflowNode[] = []
  const connections: WorkflowConnection[] = []

  for (const node of graphNodes(graph)) {
    const definition = definitions.get((node as HazorNode).hazorType)
    // Anything the catalog does not describe is skipped rather than guessed
    // at. The editor registers nothing else, so this only triggers for a graph
    // restored from a build that knew a type this one does not.
    if (!definition) {
      continue
    }
    nodes.push({
      id: documentIdFor(node),
      type: definition.type,
      properties: wireProperties(definition, node),
    })

    const inputs = node.inputs ?? []
    for (let slot = 0; slot < inputs.length; slot += 1) {
      const linkId = inputs[slot]?.link
      if (linkId == null) {
        continue
      }
      const link = graph.links[linkId]
      if (!link) {
        continue
      }
      const origin = graph.getNodeById(link.origin_id)
      const originDefinition = origin
        ? definitions.get((origin as HazorNode).hazorType)
        : undefined
      const output = originDefinition?.outputs[link.origin_slot]
      const input = inputPortForSlot(node, definition, slot)
      if (!origin || !output || !input) {
        continue
      }
      connections.push({
        from: documentIdFor(origin),
        output: output.id,
        to: documentIdFor(node),
        input,
      })
    }
  }

  return { version: catalog.workflowVersion, nodes, connections }
}

/**
 * The starting graph: ComfyUI's canonical text-to-image chain, with Hazor's
 * canvas output in place of Save Image. `key` exists because two of the nodes
 * are the same type, and `output` because the loaders have three outputs.
 */
interface StartingNode {
  key: string
  type: string
  column: number
}
const STARTING_NODES: readonly StartingNode[] = [
  { key: 'checkpoint', type: 'checkpoint', column: 0 },
  { key: 'positive', type: 'clip_text_encode', column: 1 },
  { key: 'negative', type: 'clip_text_encode', column: 1 },
  { key: 'latent', type: 'empty_latent_image', column: 1 },
  { key: 'sampler', type: 'ksampler', column: 2 },
  { key: 'decode', type: 'vae_decode', column: 3 },
  { key: 'output', type: 'image_output', column: 4 },
]
const STARTING_WIRING: ReadonlyArray<{
  from: string
  output: string
  to: string
  input: string
}> = [
  { from: 'checkpoint', output: 'clip', to: 'positive', input: 'clip' },
  { from: 'checkpoint', output: 'clip', to: 'negative', input: 'clip' },
  { from: 'checkpoint', output: 'model', to: 'sampler', input: 'model' },
  { from: 'positive', output: 'conditioning', to: 'sampler', input: 'positive' },
  { from: 'negative', output: 'conditioning', to: 'sampler', input: 'negative' },
  { from: 'latent', output: 'latent', to: 'sampler', input: 'latent' },
  { from: 'sampler', output: 'latent', to: 'decode', input: 'samples' },
  { from: 'checkpoint', output: 'vae', to: 'decode', input: 'vae' },
  { from: 'decode', output: 'image', to: 'output', input: 'image' },
]

/** The text each of the two encode nodes starts with, so their roles read. */
const STARTING_TEXT: Record<string, string> = {
  positive: '',
  negative: 'text, watermark',
}

interface PlacedNode {
  node: LGraphNode
  definition: NodeDefinition
}

/**
 * Lays out the starting graph column by column and wires it up.
 *
 * `defaultModelId` is the registry's default and may be empty — with no model
 * registered the graph is still built and still editable, and Run reports the
 * registry's own explanation of what is missing. No model is invented.
 */
export function buildStartingGraph(
  graph: LGraph,
  catalog: NodeCatalog,
  defaultModelId: string,
): void {
  graph.clear()

  const definitions = new Map(catalog.nodes.map((definition) => [definition.type, definition]))
  const placed = new Map<string, PlacedNode>()
  const columnX: number[] = []
  const columnY: number[] = []

  for (const entry of STARTING_NODES) {
    const definition = definitions.get(entry.type)
    if (!definition) {
      continue
    }
    const node = createNode(graph, definition)
    if (!node) {
      continue
    }
    const x = columnX[entry.column] ?? ORIGIN[0]
    const y = columnY[entry.column] ?? ORIGIN[1]
    node.pos = [x, y]
    columnY[entry.column] = y + node.size[1] + LiteGraph.NODE_TITLE_HEIGHT + ROW_GAP
    // Every later column starts to the right of the widest node so far.
    const right = x + node.size[0] + COLUMN_GAP
    for (let next = entry.column + 1; next < STARTING_NODES.length; next += 1) {
      columnX[next] = Math.max(columnX[next] ?? ORIGIN[0], right)
    }
    placed.set(entry.key, { node, definition })
  }

  for (const wire of STARTING_WIRING) {
    const from = placed.get(wire.from)
    const to = placed.get(wire.to)
    if (!from || !to) {
      continue
    }
    const outputSlot = from.definition.outputs.findIndex((port) => port.id === wire.output)
    const inputSlot = inputSlotForPort(to.node, wire.input)
    if (outputSlot < 0 || inputSlot < 0) {
      continue
    }
    from.node.connect(outputSlot, to.node, inputSlot)
  }

  for (const [key, text] of Object.entries(STARTING_TEXT)) {
    if (text) {
      placed.get(key)?.node.setProperty('text', text)
    }
  }

  // Name the registry's default outright when there is one, so the starting
  // graph refers to a model that exists rather than to the "default" alias.
  const checkpoint = placed.get('checkpoint')
  if (checkpoint && defaultModelId) {
    checkpoint.node.setProperty('modelId', defaultModelId)
  }
}

/**
 * Marks the nodes a validation failure named: the error border and footer
 * message `graph/canvasRenderer.ts` draws for `node.hz.state === 'error'`.
 */
export function highlightErrorNodes(
  graph: LGraph,
  errors: ReadonlyArray<{ nodeId: string; message: string }>,
): void {
  for (const error of errors) {
    if (!error.nodeId) {
      continue
    }
    const node = nodeForDocumentId(graph, error.nodeId)
    if (node && isHazorVisualNode(node)) {
      setNodeState(node, 'error', { error: error.message })
    }
  }
}

/** Clears those marks, back to the default border. */
export function clearErrorHighlights(graph: LGraph): void {
  for (const node of graphNodes(graph)) {
    if (isHazorVisualNode(node)) {
      setNodeState(node, 'default')
    }
  }
}

/**
 * Which node a run lights up. The backend reports progress for the run, not
 * per node, so the editor maps its two working states onto the node that does
 * the work in each: the checkpoint while the model loads, Generate while it
 * samples. Presentation only — nothing here is part of the contract.
 */
const RUNNING_NODE_TYPE: Partial<Record<GenerationState, string>> = {
  loading: 'checkpoint',
  generating: 'ksampler',
}

/**
 * Reflects the run onto the graph: the header badge and progress bar
 * `graph/canvasRenderer.ts` draws for `running`, and the flowing links it
 * keeps alive by stamping each incoming link while that state holds.
 */
export function showRunState(
  graph: LGraph,
  state: GenerationState,
  progress: { step: number; steps: number } | null,
): void {
  const runningType = RUNNING_NODE_TYPE[state]
  const counted = progress !== null && progress.steps > 0
  const fraction = counted ? progress.step / progress.steps : 0
  const label = nodeRunLabelFor(state, progress)

  for (const node of graphNodes(graph)) {
    // An error is the more important thing to say, and it outlives the run.
    if (!isHazorVisualNode(node) || node.hz.state === 'error') {
      continue
    }
    if (runningType && (node as HazorNode).hazorType === runningType) {
      setNodeState(node, 'running', { progress: fraction, label })
    } else if (node.hz.state === 'running') {
      setNodeState(node, 'default')
    }
  }
}
