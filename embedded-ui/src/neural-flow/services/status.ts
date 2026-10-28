/**
 * How a run state reads in the header.
 *
 * The states themselves are the backend's (`sdlocal::SdGenerationState`); this
 * only decides what to call them and which colour slot the indicator uses.
 * Loading and generating are kept apart because the first costs minutes on a
 * cold checkpoint and reports nothing, while the second reports a step count —
 * collapsing them would make a load look like a hang.
 */
import type { GenerationState } from './contract'

export type StatusKind = 'idle' | 'busy' | 'ok' | 'error'

export function statusKindFor(state: GenerationState): StatusKind {
  switch (state) {
    case 'loading':
    case 'generating':
      return 'busy'
    case 'completed':
      return 'ok'
    case 'failed':
      return 'error'
    case 'cancelled':
    case 'idle':
      return 'idle'
  }
}

export function statusLabelFor(state: GenerationState): string {
  switch (state) {
    case 'loading':
      return 'Loading model'
    case 'generating':
      return 'Generating'
    case 'completed':
      return 'Completed'
    case 'failed':
      return 'Error'
    case 'cancelled':
      return 'Cancelled'
    case 'idle':
      return 'Ready'
  }
}
