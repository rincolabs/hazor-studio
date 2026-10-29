/**
 * One Qt WebChannel connection, shared by every composable. Connecting is
 * idempotent and cached: whichever composable asks first pays for it, every
 * later caller gets the same client once it resolves.
 */
import { QtBridgeError, isHostedByQt } from '@shared/bridge/qtBridge'

import { AiCenterClient } from './aiCenter'

let pending: Promise<AiCenterClient> | null = null

export function getClient(): Promise<AiCenterClient> {
  if (!pending) {
    pending = isHostedByQt()
      ? AiCenterClient.connect()
      : Promise.reject(
          new QtBridgeError(
            'This interface runs inside Hazor. Open it from the AI Center entry points.',
          ),
        )
    // A failed connection is not a permanent verdict — let a later caller retry.
    pending.catch(() => {
      pending = null
    })
  }
  return pending
}

export function describeError(error: unknown): string {
  return error instanceof Error ? error.message : String(error)
}
