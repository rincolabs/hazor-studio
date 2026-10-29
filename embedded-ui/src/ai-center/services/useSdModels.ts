/** Stable Diffusion model registry state — reads/writes go through sdlocal::SdModelManager only. */
import { ref } from 'vue'

import type { SdModelCategory, SdModelsState, SdTextEncoderRole } from './contract'

import { describeError, getClient } from './client'

const state = ref<SdModelsState | null>(null)
const error = ref('')

let started = false
function start(): void {
  if (started) {
    return
  }
  started = true
  void getClient()
    .then(async (client) => {
      state.value = await client.sdModelsState()
      client.on('sdModelsChanged', (next) => {
        state.value = next
      })
    })
    .catch((reason: unknown) => {
      error.value = describeError(reason)
    })
}

async function registerFile(path: string, category: SdModelCategory, encoderRole: SdTextEncoderRole, id: string): Promise<void> {
  const client = await getClient()
  state.value = await client.registerSdFile(path, category, encoderRole, id)
}

async function registerDirectory(path: string, category: SdModelCategory, encoderRole: SdTextEncoderRole, recursive: boolean): Promise<void> {
  const client = await getClient()
  state.value = await client.registerSdDirectory(path, category, encoderRole, recursive)
}

async function unregisterEntry(id: string): Promise<void> {
  const client = await getClient()
  state.value = await client.unregisterSdEntry(id)
}

async function unregisterDirectory(path: string): Promise<void> {
  const client = await getClient()
  state.value = await client.unregisterSdDirectory(path)
}

async function setDefaultModel(id: string): Promise<void> {
  const client = await getClient()
  state.value = await client.setSdDefaultModel(id)
}

async function rescan(): Promise<void> {
  const client = await getClient()
  state.value = await client.rescanSdDirectories()
}

async function browseForFile(): Promise<string> {
  return (await getClient()).browseForSdFile()
}

async function browseForDirectory(): Promise<string> {
  return (await getClient()).browseForSdDirectory()
}

export function useSdModels() {
  start()
  return {
    state,
    error,
    registerFile,
    registerDirectory,
    unregisterEntry,
    unregisterDirectory,
    setDefaultModel,
    rescan,
    browseForFile,
    browseForDirectory,
  }
}
