/**
 * Preset list + editor state for one AgentKind (assistant or generative).
 * Shared shape behind Assistants and Image Generation — same backend
 * (AgentPresetManager), filtered by kind, so this composable is the one
 * place that talks to it instead of two.
 */
import { computed, ref } from 'vue'

import type {
  AgentKind,
  ConnectionTestResult,
  Preset,
  PresetListState,
  PromptTestResult,
  ProviderType,
} from './contract'

import { describeError, getClient } from './client'

function clone(preset: Preset): Preset {
  return JSON.parse(JSON.stringify(preset)) as Preset
}

export function usePresetEditor(kind: AgentKind) {
  const list = ref<PresetListState>({ presets: [], activeName: '' })
  const selectedName = ref('')
  const draft = ref<Preset | null>(null)
  const saved = ref<Preset | null>(null)
  const loading = ref(false)
  const saving = ref(false)
  const error = ref('')

  const connectionResult = ref<ConnectionTestResult | null>(null)
  const testingConnection = ref(false)
  const detectedModels = ref<string[]>([])
  const detectingModels = ref(false)
  const promptResult = ref<PromptTestResult | null>(null)
  const testingPrompt = ref(false)

  const dirty = computed(
    () => draft.value !== null && saved.value !== null && JSON.stringify(draft.value) !== JSON.stringify(saved.value),
  )

  function resetTestState(): void {
    connectionResult.value = null
    detectedModels.value = []
    promptResult.value = null
  }

  async function refreshList(): Promise<void> {
    const client = await getClient()
    const lists = await client.presetLists()
    list.value = kind === 'assistant' ? lists.assistants : lists.generative
  }

  async function select(name: string): Promise<void> {
    loading.value = true
    error.value = ''
    try {
      const client = await getClient()
      const preset = await client.getPreset(name)
      draft.value = clone(preset)
      saved.value = clone(preset)
      selectedName.value = name
      resetTestState()
    } catch (reason) {
      error.value = describeError(reason)
    } finally {
      loading.value = false
    }
  }

  async function start(): Promise<void> {
    loading.value = true
    try {
      await refreshList()
      const first = list.value.presets.find((p) => p.isActive) ?? list.value.presets[0]
      if (first) {
        await select(first.name)
      }
    } catch (reason) {
      error.value = describeError(reason)
    } finally {
      loading.value = false
    }
  }

  /** Resolves to the refusal message when the backend rejected the name. */
  async function create(name: string, description: string, providerType: ProviderType): Promise<string> {
    const client = await getClient()
    const result = await client.createPreset(kind, name, description, providerType)
    if (!result.ok) {
      return result.error
    }
    await refreshList()
    draft.value = clone(result.preset)
    saved.value = clone(result.preset)
    selectedName.value = result.preset.name
    resetTestState()
    return ''
  }

  async function duplicate(): Promise<void> {
    if (!selectedName.value) return
    const client = await getClient()
    const newName = await client.duplicatePreset(selectedName.value)
    await refreshList()
    await select(newName)
  }

  async function remove(): Promise<void> {
    if (!selectedName.value) return
    const client = await getClient()
    list.value = kind === 'assistant'
      ? (await client.deletePreset(selectedName.value)).assistants
      : (await client.deletePreset(selectedName.value)).generative
    const next = list.value.presets[0]
    if (next) {
      await select(next.name)
    } else {
      draft.value = null
      saved.value = null
      selectedName.value = ''
    }
  }

  /** False when the backend refused the save; `error` then holds the reason. */
  async function save(): Promise<boolean> {
    if (!draft.value || !selectedName.value) return false
    saving.value = true
    error.value = ''
    try {
      const client = await getClient()
      // The backend renames when draft.name differs from the stored name, and
      // refuses rather than overwriting when that name is taken.
      const result = await client.savePreset(selectedName.value, draft.value)
      if (!result.ok) {
        error.value = result.error
        return false
      }
      draft.value = clone(result.preset)
      saved.value = clone(result.preset)
      selectedName.value = result.preset.name
      await refreshList()
      return true
    } catch (reason) {
      error.value = describeError(reason)
      return false
    } finally {
      saving.value = false
    }
  }

  /**
   * Switching provider type re-prefills the endpoint and key for the new one —
   * the previous provider's URL is meaningless for it, and the defaults come
   * from ProviderConfig rather than a copy kept here ("Local" is Ollama for an
   * assistant and an A1111 server for image generation).
   */
  async function setProviderType(type: ProviderType): Promise<void> {
    const preset = draft.value
    if (!preset || preset.provider.type === type) return
    preset.provider.type = type
    detectedModels.value = [] // detected against the old endpoint
    const client = await getClient()
    const defaults = await client.providerDefaults(kind, type)
    preset.provider.baseUrl = defaults.baseUrl
    preset.provider.apiKey = defaults.apiKey
  }

  function discard(): void {
    if (saved.value) {
      draft.value = clone(saved.value)
    }
  }

  async function setActive(): Promise<void> {
    if (!selectedName.value) return
    const client = await getClient()
    await client.setActivePreset(selectedName.value, kind)
    await refreshList()
  }

  // The three tests send the draft, so they exercise the values on screen even
  // when they have not been saved yet.
  async function testConnection(): Promise<void> {
    if (!selectedName.value || !draft.value) return
    testingConnection.value = true
    connectionResult.value = null
    const client = await getClient()
    client.testConnection(selectedName.value, draft.value)
  }

  async function detectModels(): Promise<void> {
    if (!selectedName.value || !draft.value) return
    detectingModels.value = true
    const client = await getClient()
    client.detectModels(selectedName.value, draft.value)
  }

  async function testPrompt(prompt: string): Promise<void> {
    if (!selectedName.value || !draft.value) return
    testingPrompt.value = true
    promptResult.value = null
    const client = await getClient()
    client.testPrompt(selectedName.value, draft.value, prompt)
  }

  // These three results are asynchronous (the backend call is a real network
  // request) and come back as signals rather than call replies — see
  // AiCenterClient.testConnection/detectModels/testPrompt. A result for a
  // preset that is no longer selected is dropped: the user moved on.
  void getClient().then((client) => {
    client.on('connectionTestResult', (name, result) => {
      if (name === selectedName.value) {
        connectionResult.value = result
        testingConnection.value = false
      }
    })
    client.on('modelsDetected', (name, models) => {
      if (name === selectedName.value) {
        detectedModels.value = models
        detectingModels.value = false
      }
    })
    client.on('promptTestResult', (name, result) => {
      if (name === selectedName.value) {
        promptResult.value = result
        testingPrompt.value = false
      }
    })
  })

  void start()

  return {
    // The kind this editor was built for, so dialogs can offer only the
    // providers that make sense for it.
    kind,
    list,
    selectedName,
    draft,
    dirty,
    loading,
    saving,
    error,
    connectionResult,
    testingConnection,
    detectedModels,
    detectingModels,
    promptResult,
    testingPrompt,
    select,
    create,
    duplicate,
    remove,
    save,
    discard,
    setProviderType,
    setActive,
    testConnection,
    detectModels,
    testPrompt,
  }
}

export type PresetEditor = ReturnType<typeof usePresetEditor>
