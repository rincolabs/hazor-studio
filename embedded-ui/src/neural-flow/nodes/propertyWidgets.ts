/**
 * Turns one backend property spec into one LiteGraph widget, and converts
 * values between the two.
 *
 * Two value spaces meet here and must not be confused:
 *
 *   - *widget space* is what a node's `properties` hold while it is being
 *     edited. It is whatever the widget can represent: a number for a seed, a
 *     comma-separated string for a list of ids, a readable placeholder for
 *     "unset".
 *   - *wire space* is what a workflow document carries, exactly as
 *     `sdlocal::SdWorkflowValidator` expects it: a seed as a string, a list of
 *     ids as an array, an unset value as an empty string.
 *
 * `toWidgetValue` and `toWireValue` are the only crossing points. Everything
 * else — ranges, defaults, option lists — comes from the spec and is never
 * restated.
 */
import type { IWidget } from 'litegraph.js'

import type { LGraphNode } from '../graph/litegraph'
import type { PropertySpec } from '../services/contract'
import { modelIdsFor } from './modelStore'
import { addTextareaWidget } from './textareaWidget'

/**
 * Shown for an enum whose empty value means "whatever the checkpoint
 * declares" — the sampler and the scheduler both work that way. An empty combo
 * entry would read as a bug rather than as a choice.
 */
export const DEFAULT_OPTION_LABEL = '(model default)'

/** Shown for a model reference that is legitimately unset, such as the VAE. */
export const NONE_OPTION_LABEL = '(none)'

const PLACEHOLDERS = new Set([DEFAULT_OPTION_LABEL, NONE_OPTION_LABEL])

/**
 * litegraph multiplies `options.step` by 0.1 for one arrow click or drag
 * notch, so the step a human means has to be ten times bigger here.
 */
const STEP_SCALE = 10

/** Narrower than this and the long labels in this catalog stop fitting. */
export const MIN_NODE_WIDTH = 230

export function toWidgetValue(spec: PropertySpec, value: unknown): string | number | boolean {
  switch (spec.kind) {
    case 'bool':
      return Boolean(value)
    case 'int':
      return Math.round(Number(value ?? 0))
    case 'number':
      return Number(value ?? 0)
    case 'seed':
      // A number widget is worth the trade: a seed is dragged and typed far
      // more often than it exceeds 2^53, which is where a JavaScript number
      // stops being exact. The wire format stays a string either way.
      return Math.trunc(Number(value ?? -1))
    case 'enum':
      return value === '' || value == null ? DEFAULT_OPTION_LABEL : String(value)
    case 'modelRef':
      return value === '' || value == null ? NONE_OPTION_LABEL : String(value)
    case 'modelRefList':
      return Array.isArray(value) ? value.join(', ') : String(value ?? '')
    case 'string':
    case 'text':
      return String(value ?? '')
  }
}

export function toWireValue(spec: PropertySpec, value: unknown): unknown {
  switch (spec.kind) {
    case 'bool':
      return Boolean(value)
    case 'int':
      return Math.round(Number(value ?? 0))
    case 'number':
      return Number(value ?? 0)
    case 'seed':
      return String(Math.trunc(Number(value ?? -1)))
    case 'enum':
    case 'modelRef':
      return PLACEHOLDERS.has(String(value)) ? '' : String(value ?? '')
    case 'modelRefList':
      return String(value ?? '')
        .split(',')
        .map((entry) => entry.trim())
        .filter((entry) => entry.length > 0)
    case 'string':
    case 'text':
      return String(value ?? '')
  }
}

/** The options an enum widget offers: the backend's list, plus "unset" if legal. */
function enumOptions(spec: PropertySpec): string[] {
  const options = spec.options ?? []
  return spec.allowsEmpty ? [DEFAULT_OPTION_LABEL, ...options] : [...options]
}

/**
 * The options a model reference offers: the spec's own default first, then
 * every registered model that could fill it.
 *
 * The default leads so the list is never empty — with no model registered, the
 * only choice is the one the document would have carried anyway, and the
 * backend answers with the registry's own explanation of what is missing
 * instead of the editor inventing a model that does not exist.
 */
function modelOptions(spec: PropertySpec): string[] {
  const fallback = typeof spec.default === 'string' ? spec.default : ''
  const head = fallback === '' ? NONE_OPTION_LABEL : fallback
  const ids = modelIdsFor(spec.modelCategories).filter((id) => id !== fallback)
  return [head, ...ids]
}

/** Decimals to show for a given step: 0.05 → 2, 0.5 → 1, 1 → 0. */
function precisionFor(step: number): number {
  if (!Number.isFinite(step) || step <= 0) {
    return 2
  }
  return Math.min(4, Math.max(0, Math.ceil(-Math.log10(step))))
}

/**
 * Keeps a value on the multiple the backend requires — latent dimensions must
 * be multiples of 8 — so dragging the widget cannot produce a graph that fails
 * validation. The rule itself is the spec's; this only enforces it early.
 */
function snapToMultiple(spec: PropertySpec): ((value: number) => number) | null {
  const multiple = spec.multipleOf
  if (!multiple || multiple <= 0) {
    return null
  }
  const minimum = spec.minimum ?? multiple
  const maximum = spec.maximum ?? Number.MAX_SAFE_INTEGER
  return (value: number) =>
    Math.min(maximum, Math.max(minimum, Math.round(value / multiple) * multiple))
}

export function addPropertyWidget(node: LGraphNode, spec: PropertySpec): void {
  const value = toWidgetValue(spec, spec.default)
  const step = spec.step ?? 1

  switch (spec.kind) {
    case 'text':
      addTextareaWidget(node, spec.label, spec.key, String(value))
      break
    case 'string':
    case 'modelRefList':
      node.addWidget<IWidget>('text', spec.label, value, undefined, { property: spec.key })
      break
    case 'bool':
      node.addWidget<IWidget>('toggle', spec.label, value, undefined, { property: spec.key })
      break
    case 'int': {
      const widget = node.addWidget<IWidget>('number', spec.label, value, undefined, {
        property: spec.key,
        min: spec.minimum,
        max: spec.maximum,
        step: step * STEP_SCALE,
        precision: 0,
      })
      const snap = snapToMultiple(spec)
      if (snap) {
        // Attached after creation and closing over `widget`, not using `this`:
        // litegraph invokes a widget callback as a plain function call, so
        // `this` is undefined inside a module. `setProperty` syncs the widget
        // back without calling this again, so there is no recursion.
        widget.callback = (next: number) => {
          const snapped = snap(Number(next))
          if (snapped === Number(next)) {
            return
          }
          widget.value = snapped
          node.setProperty(spec.key, snapped)
        }
      }
      break
    }
    case 'number':
      node.addWidget<IWidget>('number', spec.label, value, undefined, {
        property: spec.key,
        min: spec.minimum,
        max: spec.maximum,
        step: step * STEP_SCALE,
        precision: precisionFor(step),
      })
      break
    case 'seed':
      // No upper bound: the catalog leaves one off because the backend only
      // has to parse the integer, and a clamp here would cap a legal seed.
      node.addWidget<IWidget>('number', spec.label, value, undefined, {
        property: spec.key,
        min: spec.minimum,
        step: STEP_SCALE,
        precision: 0,
      })
      break
    case 'enum':
      node.addWidget<IWidget>('combo', spec.label, value, undefined, {
        property: spec.key,
        values: () => enumOptions(spec),
      })
      break
    case 'modelRef':
      node.addWidget<IWidget>('combo', spec.label, value, undefined, {
        property: spec.key,
        // A function, not a list: the registered models change while the
        // editor is open, and litegraph calls this every time it draws the
        // widget or opens its menu.
        values: () => modelOptions(spec),
      })
      break
  }
}
