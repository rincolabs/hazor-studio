/**
 * The registered Stable Diffusion models, as the backend last reported them.
 *
 * Module state rather than a Vue ref, because the reader is a LiteGraph combo
 * widget: litegraph asks a widget for its option list through a plain function
 * every time it draws or opens it, so the list only has to be correct when
 * asked. That is also what lets a model appear or disappear without any node
 * type being registered again.
 *
 * Nothing is stored or interpreted here. The registry in C++
 * (`sdlocal::SdModelManager`) owns the files, the paths, the categories and
 * the default; this is a cache of its last answer.
 */
import type { ModelCategory, ModelEntry } from '../services/contract'

let models: ModelEntry[] = []

export function setModels(next: readonly ModelEntry[] | undefined): void {
  models = next ? [...next] : []
}

export function allModels(): readonly ModelEntry[] {
  return models
}

/**
 * The ids of the usable models in any of `categories`, in registry order.
 *
 * Unavailable entries are left out: the registry records where weights live
 * without owning them, so a file can be gone, and offering it would only buy a
 * load failure several minutes in.
 */
export function modelIdsFor(categories: readonly ModelCategory[] | undefined): string[] {
  if (!categories || categories.length === 0) {
    return []
  }
  return models
    .filter((model) => model.available && categories.includes(model.category))
    .map((model) => model.id)
}

/** True when at least one model is registered that could fill `categories`. */
export function hasModelFor(categories: readonly ModelCategory[] | undefined): boolean {
  return modelIdsFor(categories).length > 0
}
