/** MCP Server draft-and-apply state. See AiCenterBridge_Mcp.cpp / McpSettings. */
import { computed, ref } from 'vue'

import type { McpState } from './contract'

import { describeError, getClient } from './client'

const state = ref<McpState | null>(null)
const draft = ref<{ enabled: boolean; port: number } | null>(null)
const error = ref('')
const saving = ref(false)

const dirty = computed(
  () =>
    state.value !== null &&
    draft.value !== null &&
    (state.value.enabled !== draft.value.enabled || state.value.port !== draft.value.port),
)

function adopt(next: McpState): void {
  state.value = next
  if (!dirty.value) {
    draft.value = { enabled: next.enabled, port: next.port }
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
      adopt(await client.mcpState())
      client.on('mcpStateChanged', adopt)
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
    adopt(await client.applyMcpState(draft.value.enabled, draft.value.port))
  } catch (reason) {
    error.value = describeError(reason)
  } finally {
    saving.value = false
  }
}

function revert(): void {
  if (state.value) {
    draft.value = { enabled: state.value.enabled, port: state.value.port }
  }
}

export function useMcp() {
  start()
  return { state, draft, dirty, saving, error, apply, revert }
}
