/**
 * The run-state fields the renderer draws on top of a node: header badge,
 * border colour, progress bar and error footer. Separate from a node's
 * `properties` (the workflow data) because none of this is part of the
 * document the backend executes — it is what `App.vue` reflects back from
 * `workflowStateChanged` / `workflowFailed` while a run is in flight.
 */
import type { LGraphNode } from 'litegraph.js'

export type NodeRunState = 'default' | 'running' | 'error'

export interface NodeGroupLabel {
  /** y offset inside the node, in graph units. */
  y: number
  text: string
}

export interface NodeVisualState {
  /** One of the backend's node categories; drives the header tint and icon. */
  category: string
  /** Shown under the title, e.g. the loaded model's name. */
  subtitle: string
  state: NodeRunState
  progress: number
  progressLabel: string
  error: string
  groups: NodeGroupLabel[]
}

export interface HazorVisualNode extends LGraphNode {
  hz: NodeVisualState
}

export function isHazorVisualNode(node: LGraphNode): node is HazorVisualNode {
  return 'hz' in node
}

/**
 * Running / error / default state for a node — the header badge, border,
 * progress bar and footer message all come from this.
 */
export function setNodeState(
  node: HazorVisualNode,
  state: NodeRunState,
  options: { progress?: number; label?: string; error?: string } = {},
): void {
  node.hz.state = state
  node.hz.progress = options.progress ?? 0
  node.hz.progressLabel = options.label ?? ''
  node.hz.error = options.error ?? ''
  node.setSize([node.size[0], node.computeSize()[1]])
  node.setDirtyCanvas(true, true)
}
