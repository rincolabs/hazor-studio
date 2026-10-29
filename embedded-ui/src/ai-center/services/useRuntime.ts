/**
 * Runtime/Performance draft-and-apply state, shared by the two tabs (one
 * footer action bar covers both, like the approved design shows). Reads and
 * writes go through AiCenterBridge's runtime calls only — see
 * AiCenterBridge_Runtime.cpp, which is the only place settings are merged
 * onto AiRuntimeManager::instance() and applied.
 */
import { computed, ref } from 'vue'

import type { RuntimeSettings, RuntimeState } from './contract'

import { describeError, getClient } from './client'

const state = ref<RuntimeState | null>(null)
const draft = ref<RuntimeSettings | null>(null)
const error = ref('')
const saving = ref(false)

const dirty = computed(
  () =>
    state.value !== null &&
    draft.value !== null &&
    JSON.stringify(state.value.settings) !== JSON.stringify(draft.value),
)

function adopt(next: RuntimeState): void {
  state.value = next
  if (!dirty.value) {
    draft.value = { ...next.settings }
  }
}

let started = false
function start(): void {
  if (started) {
    return
  }
  started = true
  void getClient()
    .then(async (client) => {
      adopt(await client.runtimeState())
      client.on('runtimeStateChanged', adopt)
    })
    .catch((reason: unknown) => {
      error.value = describeError(reason)
    })
}

async function apply(): Promise<void> {
  if (!draft.value) {
    return
  }
  saving.value = true
  error.value = ''
  try {
    const client = await getClient()
    const next = await client.applyRuntimeSettings(draft.value)
    state.value = next
    draft.value = { ...next.settings }
  } catch (reason) {
    error.value = describeError(reason)
  } finally {
    saving.value = false
  }
}

function revert(): void {
  if (state.value) {
    draft.value = { ...state.value.settings }
  }
}

async function clearCache(): Promise<void> {
  try {
    const client = await getClient()
    adopt(await client.clearAiCache())
  } catch (reason) {
    error.value = describeError(reason)
  }
}

export function useRuntime() {
  start()
  return { state, draft, dirty, saving, error, apply, revert, clearCache }
}
