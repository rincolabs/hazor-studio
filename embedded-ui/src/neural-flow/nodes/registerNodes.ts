/**
 * Registers one LiteGraph node type per node type the backend describes.
 *
 * The editor holds no list of its own. Everything a node shows — its title,
 * its ports, their types, its widgets and their defaults — comes out of
 * `sdlocal::describeNodeCatalog()`, so a node type added in C++ appears here
 * with no edit, and one removed there cannot be built here.
 */
import type { LGraph, LGraphNode } from '../graph/litegraph'
import { LiteGraph } from '../graph/litegraph'
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

/** A node built from the catalog. `hazorType` is what a document records. */
export interface HazorNode extends LGraphNode {
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

function buildNodeClass(definition: NodeDefinition): { new (): LGraphNode } {
  const labels = inputSlotLabels(definition)
  const ports = inputSlotPorts(definition)
  const inputsById = new Map<string, PortSpec>(
    definition.inputs.map((input) => [input.id, input]),
  )

  // A plain constructor function, not a class extending LGraphNode:
  // `registerNodeType` copies LGraphNode's prototype onto whatever it is
  // given, and a real subclass's own prototype chain would shadow exactly the
  // methods it is trying to install.
  function HazorFlowNode(this: HazorNode): void {
    this.hazorType = definition.type
    this.title = definition.label
    // Widgets bind to a property by name and litegraph only binds one that
    // already exists, so every default is written before the widgets are made.
    // These hold widget-space values; `graph/workflow.ts` converts them.
    this.properties = {}
    this.serialize_widgets = true

    ports.forEach((portId, slot) => {
      const spec = inputsById.get(portId)
      if (spec) {
        this.addInput(labels[slot] as string, spec.type)
      }
    })
    for (const output of definition.outputs) {
      this.addOutput(output.id, output.type)
    }

    for (const spec of definition.properties) {
      this.properties[spec.key] = toWidgetValue(spec, spec.default)
      // A property the backend validates but cannot honour usefully is kept in
      // the document and left out of the interface.
      if (spec.editable) {
        addPropertyWidget(this, spec)
      }
    }

    const size = this.computeSize()
    this.size = [Math.max(size[0], MIN_NODE_WIDTH), size[1]]
  }

  HazorFlowNode.title = definition.label
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
