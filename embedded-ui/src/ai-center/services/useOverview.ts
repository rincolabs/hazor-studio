/**
 * Shared Overview state: used by the Overview page itself and by the side
 * nav (alert count, runtime/MCP footer rows). One fetch, one subscription,
 * no matter how many components ask for it.
 */
import { ref } from 'vue'

import type { OverviewState } from './contract'

import { describeError, getClient } from './client'

const overview = ref<OverviewState | null>(null)
const error = ref('')
let started: Promise<void> | null = null

function start(): Promise<void> {
  if (!started) {
    started = getClient()
      .then(async (client) => {
        overview.value = await client.overviewState()
        client.on('overviewChanged', (next) => {
          overview.value = next
        })
      })
      .catch((reason: unknown) => {
        error.value = describeError(reason)
      })
  }
  return started
}

export function useOverview() {
  void start()
  return { overview, error }
}
