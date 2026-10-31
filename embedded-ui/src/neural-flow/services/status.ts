/**
 * How a run state reads in the header.
 *
 * The states themselves are the backend's (`sdlocal::SdGenerationState`); this
 * only decides what to call them and which colour slot the indicator uses.
 * Loading and generating are kept apart because their bars count different
 * work: model tensors first, then denoising iterations.
 */
import type { GenerationState } from './contract'

export type StatusKind = 'idle' | 'busy' | 'ok' | 'error'
export interface RunProgress {
  step: number
  steps: number
}

function hasCountedProgress(progress: RunProgress | null): progress is RunProgress {
  return progress !== null && progress.steps > 0
}

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

/**
 * Names the real phase behind each progress bar. While loading, stable-
 * diffusion.cpp reports tensors copied into the model; while generating, it
 * reports completed denoising iterations.
 */
export function statusLabelFor(
  state: GenerationState,
  progress: RunProgress | null,
): string {
  switch (state) {
    case 'loading':
      return hasCountedProgress(progress) ? 'Loading weights' : 'Loading model'
    case 'generating':
      if (!hasCountedProgress(progress)) return 'Preparing'
      return 'Denoising'
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

export function statusDetailFor(
  state: GenerationState,
  progress: RunProgress | null,
  backendDetail: string,
): string {
  if (state === 'loading' && hasCountedProgress(progress)) {
    return `${progress.step}/${progress.steps} tensors`
  }
  if (state === 'generating') {
    if (!hasCountedProgress(progress)) return 'Prompt + latent'
    return `${progress.step}/${progress.steps} steps`
  }
  return backendDetail
}

/** Short form that fits in the active graph node's header badge. */
export function nodeRunLabelFor(
  state: GenerationState,
  progress: RunProgress | null,
): string {
  if (state === 'loading') {
    return hasCountedProgress(progress) ? `Load ${progress.step}/${progress.steps}` : 'Load model'
  }
  if (state === 'generating') {
    if (!hasCountedProgress(progress)) return 'Prepare'
    return `Denoise ${progress.step}/${progress.steps}`
  }
  return statusLabelFor(state, progress)
}
