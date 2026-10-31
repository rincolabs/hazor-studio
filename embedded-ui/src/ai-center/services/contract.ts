/**
 * The contract between the AI Center and Hazor's existing AI subsystems.
 *
 * Every type here mirrors a backend struct exactly — nothing is computed on
 * this side. Sources:
 *   - `RuntimeSettings`/`RuntimeDiagnostic`/`ExecutionProviderInfo` mirror
 *     `AiRuntimeSettings`, `AiRuntimeDiagnostic`, `AiExecutionProviderInfo`
 *     (src/ai/runtime/). Read through `AiRuntimeManager::instance()`, never a
 *     fresh `AiRuntimeSettings::load()` — see AiCenterBridge_Runtime.
 *   - `ModelDescriptor` mirrors `AiModelDescriptor` (src/ai/models/).
 *   - `Preset` mirrors `AgentConfig` (src/agent/AgentConfig.hpp).
 *   - `SdModelEntry`/`SdDirectory` mirror `sdlocal::SdModelEntry`/`SdModelDirectory`
 *     (src/ai/sd/SdModelManager.hpp).
 *   - `McpState` mirrors `McpSettings` (src/mcp/McpSettings.hpp).
 */

// ── Navigation ────────────────────────────────────────────────────────────

export type SectionId = 'overview' | 'assistants' | 'image-generation' | 'models' | 'system'

// ── Runtime (System > Runtime / Performance, Overview "AI Runtime" card) ──

export type WorkingResolutionPolicy = 'auto' | 'preserveDocument' | 'limitLongestSide'

export interface RuntimeSettings {
  enabled: boolean
  executionProvider: string
  cpuThreadCount: number
  useFp16WhenAvailable: boolean
  enableGraphOptimization: boolean
  enableMemoryArena: boolean
  enableMemoryPattern: boolean
  maxGpuMemoryMB: number
  maxCpuMemoryMB: number
  maxConcurrentAiJobs: number
  maxLoadedModels: number
  allowCpuFallback: boolean
  preloadModelsOnStartup: boolean
  embeddingCacheEnabled: boolean
  maxCachedDocuments: number
  maxCachedEmbeddingsMB: number
  workingResolution: WorkingResolutionPolicy
  workingResolutionLongestSide: number
}

export type RuntimeStatus = 'notCompiled' | 'disabled' | 'notInitialized' | 'ready' | 'error'

export interface ExecutionProviderInfo {
  id: string
  displayName: string
  supportedByPlatform: boolean
  compiledInRuntime: boolean
  dependenciesAvailable: boolean
  sessionCreationWorks: boolean
  status: string
  errorMessage: string
}

export interface RuntimeDiagnostic {
  status: RuntimeStatus
  runtimeInstalled: boolean
  runtimeVersion: string
  loadedFrom: string
  selectedProvider: string
  activeProvider: string
  lastFallback: string
  lastError: string
  loadedSessions: number
}

/** Everything the Runtime/Performance/Diagnostics tabs and the Overview card need. */
export interface RuntimeState {
  settings: RuntimeSettings
  diagnostic: RuntimeDiagnostic
  availableProviders: ExecutionProviderInfo[]
  cachedEmbeddingCount: number
  cachedEmbeddingBytes: number
  onnxRuntimeVersion: string
  /** Override for the Real-ESRGAN binary; empty means the packaged backend. */
  realesrganExecutable: string
  osName: string
  cpuArchitecture: string
  /** Aggregated from AiModelRegistry::allModels(), grouped by origin — for the Diagnostics card. */
  bundledModelCount: number
  downloadedModelCount: number
  customModelCount: number
  invalidModelCount: number
}

// ── Models — Vision & Editing (AiModelRegistry) ────────────────────────────

export type ModelOrigin = 'bundled' | 'downloaded' | 'custom'
export type ModelStatus = 'valid' | 'invalid' | 'missingFiles' | 'unsupportedSchema' | 'unsupportedBackend' | 'invalidMetadata'

export interface ModelDescriptor {
  id: string
  task: string
  family: string
  displayName: string
  description: string
  quality: string
  origin: ModelOrigin
  status: ModelStatus
  isValid: boolean
  statusReason: string
  rootDir: string
  files: string[]
  licenseName: string
  licenseFile: string
  sourceName: string
  sourceUrl: string
  isDefault: boolean
}

export interface ModelTaskGroup {
  task: string
  taskLabel: string
  models: ModelDescriptor[]
  defaultModelId: string
  /** Shown as "Not yet exposed in the app UI" (e.g. Inpainting). */
  exposedInUi: boolean
}

/** A manifest entry available to download. Only meaningful when a manifest is configured. */
export interface ManifestEntry {
  id: string
  task: string
  displayName: string
  description: string
  sizeBytes: number
}

export interface ModelsVisionState {
  taskGroups: ModelTaskGroup[]
  manifestConfigured: boolean
  manifestEntries: ManifestEntry[]
}

// ── Models — Stable Diffusion (sdlocal::SdModelManager) ────────────────────

export type SdModelCategory =
  | 'checkpoint'
  | 'diffusionModel'
  | 'textEncoder'
  | 'visionEncoder'
  | 'vae'
  | 'lora'
  | 'embedding'
  | 'controlNet'
  | 'ipAdapter'
  | 'photoMaker'
  | 'pulid'
  | 'identityEmbedding'
  | 'upscaler'
  | 'adetailer'
export type SdTextEncoderRole = 'auto' | 'clipL' | 'clipG' | 't5xxl' | 'llm' | 'llmVision' | 'clipVision'
// No 'environment' case: SdModelEntry carries no flag distinguishing an
// environment-variable-sourced pinned entry from an explicitly registered
// one, so both surface identically here (a known, flagged simplification).
export type SdEntrySource = 'directory' | 'registeredFile'

export interface SdModelEntry {
  id: string
  path: string
  category: SdModelCategory
  encoderRole: SdTextEncoderRole
  label: string
  discovered: boolean
  source: SdEntrySource
  available: boolean
  isDefault: boolean
}

export interface SdDirectory {
  path: string
  category: SdModelCategory
  encoderRole: SdTextEncoderRole
  recursive: boolean
  available: boolean
  modelsFound: number
}

export interface SdModelsState {
  entries: SdModelEntry[]
  directories: SdDirectory[]
  defaultModelId: string
  countsByCategory: Record<SdModelCategory, number>
  lastScanAt: string
}

// ── Assistants / Image Generation presets (AgentPresetManager) ────────────

export type AgentKind = 'assistant' | 'generative'
export type ProviderType = 'local' | 'openai' | 'anthropic' | 'google' | 'custom'
export type ToolMode = 'auto' | 'required' | 'none'

export interface ProviderConfig {
  type: ProviderType
  baseUrl: string
  /**
   * The real key: this page is the editor for it, so it round-trips in full.
   * Rendered in a password field with an explicit reveal toggle, never logged.
   */
  apiKey: string
  model: string
}

/** What a fresh provider of a given type starts with — resolved by ProviderConfig. */
export interface ProviderDefaults {
  baseUrl: string
  apiKey: string
}

export interface ModelSettings {
  temperature: number
  maxTokens: number
  topP: number
  frequencyPenalty: number
  presencePenalty: number
}

export interface AgentBehavior {
  systemPrompt: string
  enabledTools: string[]
  toolMode: ToolMode
}

export interface GenerativeSettings {
  steps: number
  strength: number
  seed: number
  negativePrompt: string
  promptAssistEnabled: boolean
  promptAssistPreset: string
}

export interface Preset {
  name: string
  description: string
  kind: AgentKind
  provider: ProviderConfig
  modelSettings: ModelSettings
  behavior: AgentBehavior
  generative: GenerativeSettings
  timeoutMs: number
  retries: number
  stream: boolean
  logLevel: string
  cache: boolean
  maxHistoryTurns: number
  resultFeedback: boolean
}

export interface PresetSummary {
  name: string
  kind: AgentKind
  providerType: ProviderType
  model: string
  isActive: boolean
}

export interface PresetListState {
  presets: PresetSummary[]
  activeName: string
}

/**
 * Reply shape of createPreset/savePreset. A name collision or an empty name is
 * refused by the backend rather than silently overwriting a preset, so the
 * editor needs the reason to show — `preset` is then the unchanged stored one.
 */
export interface PresetMutation {
  ok: boolean
  error: string
  preset: Preset
}

export interface ToolCatalogGroup {
  id: string
  label: string
  tools: string[]
}

export interface ConnectionTestResult {
  ok: boolean
  message: string
  elapsedMs: number
  models: string[]
}

export interface PromptTestResult {
  ok: boolean
  message: string
  elapsedSeconds: number
  response: string
}

// ── MCP (src/mcp/McpSettings.hpp) ──────────────────────────────────────────

export interface McpState {
  enabled: boolean
  port: number
  listening: boolean
}

// ── Overview ────────────────────────────────────────────────────────────

export interface OverviewAlert {
  severity: 'warn' | 'err'
  title: string
  detail: string
  /** e.g. "Open System › Runtime" — the backend owns the copy, not a client-side label map. */
  linkLabel: string
  /** Section/subsection the link navigates to. */
  section: SectionId
  subsection: string
}

export interface OverviewState {
  alerts: OverviewAlert[]
  runtime: RuntimeState
  models: { registered: number; tasks: number; valid: number; invalid: number; invalidNames: string[] }
  activeAssistant: PresetSummary | null
  activeGenerative: PresetSummary | null
  sd: { registered: number; categories: number; directories: number; unavailableDirectories: number; missingPaths: string[]; defaultModelId: string }
  mcp: McpState
}

/** Where `AiCenterWindow::openSection()` wants the page to land on first load. */
export interface NavigationTarget {
  section: SectionId
  subsection: string
}
