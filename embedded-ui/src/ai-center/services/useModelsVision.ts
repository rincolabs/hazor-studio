/** Vision & Editing model state — reads/writes go through AiModelRegistry only. */
import { ref } from 'vue'

import type { ModelsVisionState } from './contract'

import { describeError, getClient } from './client'

const state = ref<ModelsVisionState | null>(null)
const error = ref('')

let started = false
function start(): void {
  if (started) {
    return
  }
  started = true
  void getClient()
    .then(async (client) => {
      state.value = await client.modelsVisionState()
      client.on('modelsVisionChanged', (next) => {
        state.value = next
      })
    })
    .catch((reason: unknown) => {
      error.value = describeError(reason)
    })
}

async function setDefaultModel(task: string, modelId: string): Promise<void> {
  const client = await getClient()
  state.value = await client.setDefaultModel(task, modelId)
}

async function removeInstalledModel(modelId: string): Promise<void> {
  const client = await getClient()
  state.value = await client.removeInstalledModel(modelId)
}

async function showModelFiles(modelId: string): Promise<void> {
  (await getClient()).showModelFiles(modelId)
}

async function openModelLicense(modelId: string): Promise<void> {
  (await getClient()).openModelLicense(modelId)
}

export function useModelsVision() {
  start()
  return { state, error, setDefaultModel, removeInstalledModel, showModelFiles, openModelLicense }
}
