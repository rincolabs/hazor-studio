/**
 * How a run state reads in the header.
 *
 * The states themselves are the backend's (`sdlocal::SdGenerationState`); this
 * only decides what to call them and which colour slot the indicator uses.
 * Loading and generating are kept apart because their bars count different
 * work: model tensors first, then sampling iterations.
 */
import type { GenerationState } from './contract'

export type StatusKind = 'idle' | 'busy' | 'ok' | 'error'
export interface RunProgress {
  step: number
  steps: number
}

/**
 * A `generating` pass that is not sampling at all.
 *
 * `SdGenerationState` has a single state for every kind of work in flight, so
 * the operation behind it travels as the `detail` of `stateChanged`:
 * `SdGenerationService::run()` names the two passes that never sample ("Canny",
 * "Upscale") and leaves the detail empty for the sampling path. Without this
 * table both of them read as denoising — and neither reports a single step,
 * which is also why `detail` here replaces the step counter rather than
 * competing with it.
 *
 * Matching that string is the honest limit of a frontend-only fix: it is a
 * user-presentable string on the C++ side, so a translated build stops matching
 * and falls back to the sampling wording. Naming the operation
 * machine-readably on the signal is the backend's job, not this file's.
 */
interface RunPhase {
  label: string
  detail: string
  /** `hazorType` of the node doing the work, so the badge lands on it. */
  nodeType: string
}

const RUN_PHASES: Record<string, RunPhase> = {
  canny: { label: 'Canny edges', detail: 'No steps reported', nodeType: 'canny' },
  upscale: { label: 'Upscaling', detail: 'No steps reported', nodeType: 'upscale' },
}

/** The non-sampling pass `detail` names, or null for sampling and every other state. */
export function runPhaseFor(state: GenerationState, detail: string): RunPhase | null {
  if (state !== 'generating') return null
  return RUN_PHASES[detail.trim().toLowerCase()] ?? null
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
 * diffusion.cpp reports tensors copied into the model; while sampling, it
 * reports completed denoising iterations — per pass, so a Hi-Res Fix or an
 * ADetailer face restarts the count under the same label.
 */
export function statusLabelFor(
  state: GenerationState,
  progress: RunProgress | null,
  detail: string,
): string {
  const phase = runPhaseFor(state, detail)
  if (phase) return phase.label
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
  const phase = runPhaseFor(state, backendDetail)
  if (phase) return phase.detail
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
  detail: string,
): string {
  const phase = runPhaseFor(state, detail)
  if (phase) return phase.label
  if (state === 'loading') {
    return hasCountedProgress(progress) ? `Load ${progress.step}/${progress.steps}` : 'Load model'
  }
  if (state === 'generating') {
    if (!hasCountedProgress(progress)) return 'Prepare'
    return `Denoise ${progress.step}/${progress.steps}`
  }
  return statusLabelFor(state, progress, detail)
}
