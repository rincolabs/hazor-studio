/**
 * The contract between this editor and Hazor's Stable Diffusion module.
 *
 * Every type here mirrors something the backend produces or consumes:
 *
 *   - `NodeCatalog` is `sdlocal::describeNodeCatalog()` (src/ai/sd/SdNodeCatalog.cpp),
 *     delivered by `NeuralFlowBridge::nodeDefinitions()`.
 *   - `ModelEntry` is one row of `SdModelManager::models()`.
 *   - `WorkflowDocument` is what `sdlocal::SdWorkflowDefinition::fromJson()` parses.
 *
 * These are declarations, not a source of truth. The editor holds no list of
 * node types, no property defaults and no model paths of its own: it draws
 * whatever the catalog describes, which is why a node type added in C++ shows
 * up here without a second edit.
 */

/** `sdlocal::SdPropertyKind`. */
export type PropertyKind =
  | 'string'
  | 'text'
  | 'bool'
  | 'int'
  | 'number'
  | 'seed'
  | 'enum'
  | 'modelRef'
  | 'modelRefList'

/** `sdlocal::SdModelCategory`. */
export type ModelCategory =
  | 'checkpoint'
  | 'diffusion_model'
  | 'text_encoder'
  | 'vision_encoder'
  | 'vae'
  | 'lora'
  | 'embedding'
  | 'control_net'
  | 'ip_adapter'
  | 'photo_maker'
  | 'pulid'
  | 'identity_embedding'
  | 'upscaler'
  | 'adetailer'

/** `sdlocal::SdGenerationState`. */
export type GenerationState =
  | 'idle'
  | 'loading'
  | 'generating'
  | 'completed'
  | 'failed'
  | 'cancelled'

/** One port of a node's fixed signature. `type` is a `sdlocal::SdPortType` name. */
export interface PortSpec {
  id: string
  type: string
  required: boolean
  /** Accepts an unbounded number of incoming connections. */
  multiple: boolean
}

/** One editable (or contract-only) property of a node. */
export interface PropertySpec {
  key: string
  label: string
  kind: PropertyKind
  /** What the executor uses when the document omits the key. */
  default: unknown
  /**
   * False for a property that is validated but that this build cannot honour
   * usefully; `hint` says why. Not offered as a widget.
   */
  editable: boolean
  hint?: string
  /** `int` and `number` only, and exactly what the backend validator enforces. */
  minimum?: number
  maximum?: number
  step?: number
  multipleOf?: number
  /** `enum` only. */
  options?: string[]
  allowsEmpty?: boolean
  strictOptions?: boolean
  /** `modelRef` and `modelRefList` only. */
  modelCategories?: ModelCategory[]
}

export interface NodeDefinition {
  /** The name that goes into a document's `type`, e.g. `checkpoint`. */
  type: string
  label: string
  category: string
  inputs: PortSpec[]
  outputs: PortSpec[]
  properties: PropertySpec[]
}

export interface NodeCategory {
  id: string
  label: string
}

export interface NodeCatalog {
  /** The `version` a document must carry; refused by the backend if newer. */
  workflowVersion: number
  categories: NodeCategory[]
  nodes: NodeDefinition[]
  portTypes: string[]
  modelCategories: ModelCategory[]
}

/** One registered weights file, as the model registry records it. */
export interface ModelEntry {
  /** The stable reference a workflow stores. */
  id: string
  label: string
  category: ModelCategory
  encoderRole: string
  path: string
  /** The file was still on disk when the registry was last asked. */
  available: boolean
  /** Found by scanning a registered directory rather than pinned by the user. */
  discovered: boolean
  isDefault: boolean
}

/** One ggml device the machine registered, as `SdBackend::availableDevices()`. */
export interface DeviceEntry {
  /** The exact token `sd_ctx_params_t::backend` takes, e.g. "CUDA0". */
  name: string
  description: string
  gpu: boolean
  totalMiB: number
}

/** Hazor's active theme, flattened for CSS (src/theme/WebThemeVariables.cpp). */
export type ThemeVariables = Record<string, string | number>

export interface InitialState {
  nodeDefinitions: NodeCatalog
  models: ModelEntry[]
  defaultModelId: string
  theme: ThemeVariables
  busy: boolean
  hasDocument: boolean
  /** Compiled backend, devices found and device in use, in one line. */
  backend: string
  devices: DeviceEntry[]
  /** The pinned device name, or "" for "resolve by preference". */
  pinnedDevice: string
  modelConfigPath: string
}

/** One `sdlocal::SdWorkflowError`. `nodeId` and `portId` can be empty. */
export interface WorkflowError {
  code: string
  nodeId: string
  portId: string
  message: string
}

export interface ExecuteResponse {
  started: boolean
  errors: WorkflowError[]
}

/** What a finished run reports. `seed` is a string: 64 bits, not a JSON number. */
export interface CompletedInfo {
  layerName: string
  width: number
  height: number
  seed: string
  modelId: string
  modelVersion: string
  device: string
  gpu: boolean
  cpuFallback: boolean
  backendNote: string
  loadMs: number
  contextReused: boolean
  generateMs: number
}

export interface WorkflowNode {
  id: string
  type: string
  properties: Record<string, unknown>
}

export interface WorkflowConnection {
  from: string
  output: string
  to: string
  input: string
}

/**
 * The executable document. Deliberately not what LiteGraph serialises:
 * `graph/workflow.ts` converts between the two, because the editor's own
 * format carries positions, link ids and widget values that mean nothing to
 * the backend, and the backend's carries named ports that LiteGraph stores as
 * slot indices.
 */
export interface WorkflowDocument {
  version: number
  nodes: WorkflowNode[]
  connections: WorkflowConnection[]
}
