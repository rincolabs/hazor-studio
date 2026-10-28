/**
 * The Neural Flow half of the bridge: the operations and events this panel
 * has, on top of the generic Qt transport in `@shared/bridge/qtBridge`.
 *
 * Every method maps one-to-one onto a `Q_INVOKABLE` of `NeuralFlowBridge`
 * (src/ui/NeuralFlowBridge.hpp). Nothing is computed here — no validation, no
 * model lookup, no generation — because all of it already exists in C++ and a
 * second implementation would be a second answer.
 */
import {
  invoke,
  onSignal,
  remoteObject,
  send,
  type QtRemoteObject,
} from '@shared/bridge/qtBridge'

import type {
  CompletedInfo,
  ExecuteResponse,
  GenerationState,
  InitialState,
  ModelEntry,
  NodeCatalog,
  ThemeVariables,
  WorkflowDocument,
  WorkflowError,
} from './contract'

/** The name NeuralFlowPanel registers the bridge under. */
const BRIDGE_OBJECT = 'neuralFlow'

/** Signal name → the arguments it carries. */
export interface NeuralFlowEventMap {
  workflowStarted: []
  workflowStateChanged: [state: GenerationState, detail: string]
  workflowProgress: [step: number, steps: number, seconds: number]
  workflowCompleted: [result: CompletedInfo]
  workflowFailed: [errors: WorkflowError[]]
  modelsChanged: [models: ModelEntry[]]
  documentAvailabilityChanged: [available: boolean]
  themeChanged: [theme: ThemeVariables]
}

export class NeuralFlowClient {
  private constructor(private readonly bridge: QtRemoteObject) {}

  static async connect(): Promise<NeuralFlowClient> {
    return new NeuralFlowClient(await remoteObject(BRIDGE_OBJECT))
  }

  /** The node contract, the models, the theme and the run state, in one call. */
  initialState(): Promise<InitialState> {
    return invoke<InitialState>(this.bridge, 'initialState')
  }

  nodeDefinitions(): Promise<NodeCatalog> {
    return invoke<NodeCatalog>(this.bridge, 'nodeDefinitions')
  }

  availableModels(): Promise<ModelEntry[]> {
    return invoke<ModelEntry[]>(this.bridge, 'availableModels')
  }

  /** Rescans the registered directories. The only slow call here. */
  refreshModels(): Promise<ModelEntry[]> {
    return invoke<ModelEntry[]>(this.bridge, 'refreshModels')
  }

  /**
   * Hands the document to the workflow controller, which validates it and
   * starts the run. A refusal comes back here; a run that started reports
   * through `workflowStateChanged` / `workflowCompleted` / `workflowFailed`.
   */
  executeWorkflow(document: WorkflowDocument): Promise<ExecuteResponse> {
    return invoke<ExecuteResponse>(this.bridge, 'executeWorkflow', [JSON.stringify(document)])
  }

  cancelWorkflow(): void {
    send(this.bridge, 'cancelWorkflow')
  }

  /** Puts a frontend failure into Hazor's log, where someone will see it. */
  reportError(context: string, message: string): void {
    send(this.bridge, 'reportError', [context, message])
  }

  on<Event extends keyof NeuralFlowEventMap>(
    event: Event,
    handler: (...args: NeuralFlowEventMap[Event]) => void,
  ): () => void {
    return onSignal(this.bridge, event, handler as (...args: unknown[]) => void)
  }
}
