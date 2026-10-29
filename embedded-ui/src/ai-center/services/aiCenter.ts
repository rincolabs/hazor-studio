/**
 * The AI Center half of the bridge: operations on top of the generic Qt
 * transport in `@shared/bridge/qtBridge`. Every method maps one-to-one onto a
 * `Q_INVOKABLE` of `AiCenterBridge` (src/ui/AiCenterBridge.hpp) — nothing here
 * duplicates backend logic, it only shapes the call and its reply.
 */
import { invoke, onSignal, remoteObject, send, type QtRemoteObject } from '@shared/bridge/qtBridge'

import type {
  AgentKind,
  ConnectionTestResult,
  McpState,
  ModelsVisionState,
  NavigationTarget,
  OverviewState,
  Preset,
  PresetListState,
  PromptTestResult,
  RuntimeSettings,
  RuntimeState,
  SdModelCategory,
  SdModelsState,
  SdTextEncoderRole,
  SectionId,
  ToolCatalogGroup,
} from './contract'

const BRIDGE_OBJECT = 'aiCenter'

export interface AiCenterEventMap {
  navigationRequested: [section: SectionId, subsection: string]
  overviewChanged: [state: OverviewState]
  runtimeStateChanged: [state: RuntimeState]
  modelsVisionChanged: [state: ModelsVisionState]
  sdModelsChanged: [state: SdModelsState]
  presetsChanged: [lists: { assistants: PresetListState; generative: PresetListState }]
  mcpStateChanged: [state: McpState]
  // Fire-and-forget operations (testConnection/detectModels/testPrompt below):
  // the underlying LLMClient call is genuinely asynchronous (QNetworkReply),
  // so the Q_INVOKABLE only starts it — the result arrives here, named so a
  // listener can ignore a result for a preset it no longer has open.
  connectionTestResult: [name: string, result: ConnectionTestResult]
  modelsDetected: [name: string, models: string[]]
  promptTestResult: [name: string, result: PromptTestResult]
}

export class AiCenterClient {
  private constructor(private readonly bridge: QtRemoteObject) {}

  static async connect(): Promise<AiCenterClient> {
    return new AiCenterClient(await remoteObject(BRIDGE_OBJECT))
  }

  /** Where to land on first paint — null outside a deep link (plain "open AI Center"). */
  initialTarget(): Promise<NavigationTarget | null> {
    return invoke<NavigationTarget | null>(this.bridge, 'initialTarget')
  }

  /** MCP tools available to a preset's Behavior tab. Static for the process lifetime. */
  toolCatalog(): Promise<ToolCatalogGroup[]> {
    return invoke<ToolCatalogGroup[]>(this.bridge, 'toolCatalog')
  }

  on<Event extends keyof AiCenterEventMap>(
    event: Event,
    handler: (...args: AiCenterEventMap[Event]) => void,
  ): () => void {
    return onSignal(this.bridge, event, handler as (...args: unknown[]) => void)
  }

  /** Puts a frontend failure into Hazor's log. */
  reportError(context: string, message: string): void {
    send(this.bridge, 'reportError', [context, message])
  }

  // ── Overview ──────────────────────────────────────────────────────────────

  overviewState(): Promise<OverviewState> {
    return invoke<OverviewState>(this.bridge, 'overviewState')
  }

  // ── Runtime / Performance / Diagnostics ──────────────────────────────────

  runtimeState(): Promise<RuntimeState> {
    return invoke<RuntimeState>(this.bridge, 'runtimeState')
  }

  /** Merges `patch` onto the live settings and applies it in one call — see AiRuntimeManager::setSettings. */
  applyRuntimeSettings(patch: Partial<RuntimeSettings>): Promise<RuntimeState> {
    return invoke<RuntimeState>(this.bridge, 'applyRuntimeSettings', [patch])
  }

  clearAiCache(): Promise<RuntimeState> {
    return invoke<RuntimeState>(this.bridge, 'clearAiCache')
  }

  // ── Models — Vision & Editing ────────────────────────────────────────────

  modelsVisionState(): Promise<ModelsVisionState> {
    return invoke<ModelsVisionState>(this.bridge, 'modelsVisionState')
  }

  setDefaultModel(task: string, modelId: string): Promise<ModelsVisionState> {
    return invoke<ModelsVisionState>(this.bridge, 'setDefaultModel', [task, modelId])
  }

  removeInstalledModel(modelId: string): Promise<ModelsVisionState> {
    return invoke<ModelsVisionState>(this.bridge, 'removeInstalledModel', [modelId])
  }

  showModelFiles(modelId: string): void {
    send(this.bridge, 'showModelFiles', [modelId])
  }

  openModelLicense(modelId: string): void {
    send(this.bridge, 'openModelLicense', [modelId])
  }

  // ── Models — Stable Diffusion ─────────────────────────────────────────────

  sdModelsState(): Promise<SdModelsState> {
    return invoke<SdModelsState>(this.bridge, 'sdModelsState')
  }

  registerSdFile(path: string, category: SdModelCategory, encoderRole: SdTextEncoderRole, id: string): Promise<SdModelsState> {
    return invoke<SdModelsState>(this.bridge, 'registerSdFile', [path, category, encoderRole, id])
  }

  registerSdDirectory(path: string, category: SdModelCategory, encoderRole: SdTextEncoderRole, recursive: boolean): Promise<SdModelsState> {
    return invoke<SdModelsState>(this.bridge, 'registerSdDirectory', [path, category, encoderRole, recursive])
  }

  unregisterSdEntry(id: string): Promise<SdModelsState> {
    return invoke<SdModelsState>(this.bridge, 'unregisterSdEntry', [id])
  }

  unregisterSdDirectory(path: string): Promise<SdModelsState> {
    return invoke<SdModelsState>(this.bridge, 'unregisterSdDirectory', [path])
  }

  setSdDefaultModel(id: string): Promise<SdModelsState> {
    return invoke<SdModelsState>(this.bridge, 'setSdDefaultModel', [id])
  }

  rescanSdDirectories(): Promise<SdModelsState> {
    return invoke<SdModelsState>(this.bridge, 'rescanSdDirectories')
  }

  browseForSdFile(): Promise<string> {
    return invoke<string>(this.bridge, 'browseForSdFile')
  }

  browseForSdDirectory(): Promise<string> {
    return invoke<string>(this.bridge, 'browseForSdDirectory')
  }

  // ── Presets (Assistants / Image Generation) ───────────────────────────────

  presetLists(): Promise<{ assistants: PresetListState; generative: PresetListState }> {
    return invoke(this.bridge, 'presetLists')
  }

  getPreset(name: string): Promise<Preset> {
    return invoke<Preset>(this.bridge, 'getPreset', [name])
  }

  createPreset(kind: AgentKind, name: string, description: string, providerType: Preset['provider']['type']): Promise<Preset> {
    return invoke<Preset>(this.bridge, 'createPreset', [kind, name, description, providerType])
  }

  duplicatePreset(name: string): Promise<string> {
    return invoke<string>(this.bridge, 'duplicatePreset', [name])
  }

  deletePreset(name: string): Promise<{ assistants: PresetListState; generative: PresetListState }> {
    return invoke(this.bridge, 'deletePreset', [name])
  }

  savePreset(name: string, preset: Preset): Promise<Preset> {
    return invoke<Preset>(this.bridge, 'savePreset', [name, preset])
  }

  setActivePreset(name: string, kind: AgentKind): Promise<{ assistants: PresetListState; generative: PresetListState }> {
    return invoke(this.bridge, 'setActivePreset', [name, kind])
  }

  /** Starts the test; the result arrives via the `connectionTestResult` signal. */
  testConnection(name: string): void {
    send(this.bridge, 'testConnection', [name])
  }

  /** Starts detection; the result arrives via the `modelsDetected` signal. */
  detectModels(name: string): void {
    send(this.bridge, 'detectModels', [name])
  }

  /** Starts the run; the result arrives via the `promptTestResult` signal. */
  testPrompt(name: string, prompt: string): void {
    send(this.bridge, 'testPrompt', [name, prompt])
  }

  // ── MCP ───────────────────────────────────────────────────────────────────

  mcpState(): Promise<McpState> {
    return invoke<McpState>(this.bridge, 'mcpState')
  }

  applyMcpState(enabled: boolean, port: number): Promise<McpState> {
    return invoke<McpState>(this.bridge, 'applyMcpState', [enabled, port])
  }
}
