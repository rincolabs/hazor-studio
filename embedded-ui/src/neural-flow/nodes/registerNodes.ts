/**
 * Registers one LiteGraph node type per node type the backend describes.
 *
 * The editor holds no list of its own. Everything a node shows — its title,
 * its ports, their types, its widgets and their defaults — comes out of
 * `sdlocal::describeNodeCatalog()`, so a node type added in C++ appears here
 * with no edit, and one removed there cannot be built here.
 *
 * Slot rows, the category tint and the optional-input grouping are laid out
 * here too (`node.hz`, read by `graph/canvasRenderer.ts`); which property (if
 * any) a node shows as its header subtitle is this editor's own presentation
 * choice, not part of the backend contract.
 */
import type { LGraph, LGraphNode } from '../graph/litegraph'
import { LiteGraph } from '../graph/litegraph'
import type { HazorInputSlot, HazorOutputSlot, HazorSlotExtra } from '../graph/litegraphInternals'
import { nodeWidgets } from '../graph/litegraphInternals'
import { T } from '../graph/canvasTheme'
import type { HazorVisualNode, NodeGroupLabel } from '../graph/nodeVisualState'
import type { NodeCatalog, NodeDefinition, PortSpec } from '../services/contract'
import { MIN_NODE_WIDTH, addPropertyWidget, toWidgetValue } from './propertyWidgets'

/**
 * Namespace for everything this editor registers, so the "Add node" menu
 * groups by the catalog's own categories: `hazor/<category>/<type>`.
 */
const NODE_TYPE_PREFIX = 'hazor'

/**
 * How many slots an input that accepts several connections gets.
 *
 * LiteGraph has no multi-connection input: an input slot holds one link, and
 * connecting a second replaces the first. The backend's LoRA input does accept
 * a stack, so it is given a fixed handful of slots that all map back to the
 * same port — a stack deeper than this needs a hand-written document.
 */
const MULTI_INPUT_SLOTS = 3

/**
 * The group label an optional input's row is shown under (e.g. "Optional ·
 * LoRA"). Purely presentational — a port this editor leaves ungrouped is
 * still drawn, just without the small caps header above it — and derived
 * from the port id rather than restating anything the backend validates.
 */
const OPTIONAL_GROUP_LABELS: Record<string, string> = {
  lora: 'LoRA',
  init_image: 'Image-to-image',
  mask: 'Image-to-image',
}

/** Which property (if any) a category's nodes show under their title. */
const SUBTITLE_PROPERTY: Record<string, string> = {
  checkpoint: 'modelId',
  lora: 'modelId',
}

/** A node built from the catalog. `hazorType` is what a document records. */
export interface HazorNode extends LGraphNode, HazorVisualNode {
  hazorType: string
}

export function litegraphTypeFor(definition: NodeDefinition): string {
  return `${NODE_TYPE_PREFIX}/${definition.category}/${definition.type}`
}

/**
 * The backend port id behind each input slot, by slot index.
 *
 * LiteGraph identifies a connection by slot index; the backend identifies it
 * by port name. This mapping is the whole of the difference, and it is derived
 * from the definition rather than stored on the slots, so it survives a
 * serialise/restore that would drop anything extra hung off them.
 */
export function inputSlotPorts(definition: NodeDefinition): string[] {
  const ports: string[] = []
  for (const input of definition.inputs) {
    const slots = input.multiple ? MULTI_INPUT_SLOTS : 1
    for (let index = 0; index < slots; index += 1) {
      ports.push(input.id)
    }
  }
  return ports
}

/** Labels for those slots: a repeated port is numbered. */
function inputSlotLabels(definition: NodeDefinition): string[] {
  const labels: string[] = []
  for (const input of definition.inputs) {
    const slots = input.multiple ? MULTI_INPUT_SLOTS : 1
    for (let index = 0; index < slots; index += 1) {
      labels.push(slots === 1 ? input.id : `${input.id} ${index + 1}`)
    }
  }
  return labels
}

/**
 * Places every input/output row (`slot.pos`, litegraph's own "hard-coded
 * pos" override) and the "Optional · <group>" labels between them, then
 * records where the widgets start.
 */
function layoutSlots(node: HazorNode): void {
  const inputs = node.inputs
  const outputs = node.outputs
  const rows = Math.max(inputs.length, outputs.length)
  const groups: NodeGroupLabel[] = []
  let y = T.padTop
  for (let index = 0; index < rows; index += 1) {
    const input = inputs[index] as HazorInputSlot | undefined
    const previous = inputs[index - 1] as HazorInputSlot | undefined
    if (input?.group && input.group !== previous?.group) {
      groups.push({ y: y + 13, text: `Optional · ${input.group}` })
      y += T.groupH
    }
    const cy = y + T.rowH / 2
    if (input) input.pos = [0, cy]
    const output = outputs[index] as HazorOutputSlot | undefined
    if (output) output.pos = [node.size[0], cy]
    y += T.rowH + T.rowGap
  }
  node.hz.groups = groups
  node.widgets_start_y = rows ? y : T.padTop - 2
}

/** Height = slot rows + widgets (+ error footer). Width keeps whatever was set. */
function computeNodeSize(this: HazorNode): [number, number] {
  const width = this.size?.[0] || MIN_NODE_WIDTH
  let height = this.widgets_start_y != null ? this.widgets_start_y + 2 : T.padTop
  for (const widget of nodeWidgets(this) ?? []) {
    const widgetHeight = widget.computeSize ? widget.computeSize(width)[1] : T.rowH
    height += widgetHeight + T.rowGap
  }
  height += 6
  if (this.hz.state === 'error' && this.hz.error) height += 36
  return [width, height]
}

function subtitleFor(node: HazorNode, property: string | undefined): string {
  if (!property) return ''
  const value = node.properties[property]
  if (value == null || value === '') return ''
  return String(value).replace(/\.(safetensors|ckpt)$/, '')
}

function buildNodeClass(definition: NodeDefinition): { new (): LGraphNode } {
  const labels = inputSlotLabels(definition)
  const ports = inputSlotPorts(definition)
  const inputsById = new Map<string, PortSpec>(
    definition.inputs.map((input) => [input.id, input]),
  )
  const subtitleProperty = SUBTITLE_PROPERTY[definition.type]

  // A plain constructor function, not a class extending LGraphNode:
  // `registerNodeType` copies LGraphNode's prototype onto whatever it is
  // given, and a real subclass's own prototype chain would shadow exactly the
  // methods it is trying to install.
  function HazorFlowNode(this: HazorNode): void {
    // _ctor never runs, and litegraph only creates these inside the first add*.
    this.inputs = []
    this.outputs = []
    this.hazorType = definition.type
    this.title = definition.label
    this.hz = {
      category: definition.category,
      subtitle: '',
      state: 'default',
      progress: 0,
      progressLabel: '',
      error: '',
      groups: [],
    }
    // Widgets bind to a property by name and litegraph only binds one that
    // already exists, so every default is written before the widgets are made.
    // These hold widget-space values; `graph/workflow.ts` converts them.
    this.properties = {}
    this.serialize_widgets = true

    ports.forEach((portId, slot) => {
      const spec = inputsById.get(portId)
      if (!spec) return
      const extra: HazorSlotExtra = { optional: !spec.required, group: OPTIONAL_GROUP_LABELS[portId] }
      this.addInput(labels[slot] as string, spec.type, extra)
    })
    for (const output of definition.outputs) {
      this.addOutput(output.id, output.type)
    }
    layoutSlots(this)

    for (const spec of definition.properties) {
      this.properties[spec.key] = toWidgetValue(spec, spec.default)
      // A property the backend validates but cannot honour usefully is kept in
      // the document and left out of the interface.
      if (spec.editable) {
        addPropertyWidget(this, spec)
      }
    }
    this.hz.subtitle = subtitleFor(this, subtitleProperty)

    const size = this.computeSize()
    this.size = [Math.max(size[0], MIN_NODE_WIDTH), size[1]]
  }

  HazorFlowNode.title = definition.label
  HazorFlowNode.prototype.computeSize = computeNodeSize
  HazorFlowNode.prototype.onResize = function (this: HazorNode): void {
    // Outputs stay glued to the right edge when the node is resized.
    for (const output of this.outputs as HazorOutputSlot[]) {
      const pos = output.pos
      if (pos) pos[0] = this.size[0]
    }
  }
  HazorFlowNode.prototype.onPropertyChanged = function (this: HazorNode): void {
    this.hz.subtitle = subtitleFor(this, subtitleProperty)
  }
  HazorFlowNode.prototype.onConfigure = function (this: HazorNode): void {
    // After a clone (copy/paste): redo the layout-dependent fields a plain
    // `configure()` does not restore on its own.
    layoutSlots(this)
    this.hz.subtitle = subtitleFor(this, subtitleProperty)
  }

  // registerNodeType also files the class under LiteGraph.Nodes[class.name].
  // Every one of these is the same function declaration, so without this they
  // would all land under one key and only the last would be reachable there.
  // Nothing in the editor resolves a node that way — `registered_node_types`,
  // keyed by type, is what it uses — but a registry that silently holds one of
  // nine entries is a trap for whoever looks at it next.
  Object.defineProperty(HazorFlowNode, 'name', {
    value: `Hazor_${definition.type}`,
    configurable: true,
  })
  return HazorFlowNode as unknown as { new (): LGraphNode }
}

/**
 * Replaces the registered types with the catalog's.
 *
 * A clean slate rather than an addition: reconnecting must not leave two
 * generations of the same type behind. Nothing of the library's own is lost —
 * the core bundle registers no node types at all.
 */
export function registerCatalogNodes(catalog: NodeCatalog): void {
  LiteGraph.clearRegisteredTypes()
  for (const definition of catalog.nodes) {
    LiteGraph.registerNodeType(litegraphTypeFor(definition), buildNodeClass(definition))
  }
}

/** Creates a registered node, or null when the catalog has no such type. */
export function createNode(graph: LGraph, definition: NodeDefinition): LGraphNode | null {
  const node = LiteGraph.createNode(litegraphTypeFor(definition))
  if (!node) {
    return null
  }
  graph.add(node)
  return node
}
