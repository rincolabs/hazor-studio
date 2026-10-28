/**
 * A real multiline editor embedded in a LiteGraph node.
 *
 * LiteGraph 0.7 renders its widgets into a canvas. That is fine for buttons
 * and number fields, but a prompt needs the browser's native caret, selection,
 * clipboard, line wrapping and scrolling. The custom widget below reserves
 * space in the node while this manager keeps one DOM textarea aligned with
 * that space through the canvas transform.
 */
import type { IWidget, Vector4 } from 'litegraph.js'

import type { LGraph, LGraphCanvas, LGraphNode } from '../graph/litegraph'

interface TextareaWidgetOptions {
  property: string
  label: string
}

interface TextareaWidget extends IWidget<string, TextareaWidgetOptions> {
  hazorTextarea: true
}

interface TextareaEntry {
  node: LGraphNode
  wrapper: HTMLDivElement
  textarea: HTMLTextAreaElement
  frame: number
}

type RenderHook = (surface: HTMLCanvasElement, context: CanvasRenderingContext2D) => void
type ClearHook = () => void
type ForegroundHook = (context: CanvasRenderingContext2D, visibleArea: Vector4) => void

interface CanvasHooks {
  onRender?: RenderHook
  onClear?: ClearHook
  onDrawForeground: ForegroundHook | null
}

interface GraphHooks {
  onNodeRemoved?: (node: LGraphNode) => void
}

const managers = new WeakMap<LGraph, TextareaOverlayManager>()

/** Must match the height reserved by `computeSize`. */
const TEXTAREA_HEIGHT = 122

/** LiteGraph uses the same inset for its built-in widgets. */
const WIDGET_MARGIN = 15

class TextareaOverlayManager {
  private readonly entries = new Map<TextareaWidget, TextareaEntry>()
  private frame = 0

  constructor(
    private readonly host: HTMLDivElement,
    private readonly surface: HTMLCanvasElement,
  ) {}

  beginFrame(): void {
    this.frame += 1
  }

  layout(
    widget: TextareaWidget,
    node: LGraphNode,
    context: CanvasRenderingContext2D,
    width: number,
    y: number,
  ): void {
    const entry = this.entries.get(widget) ?? this.createEntry(widget, node)
    entry.frame = this.frame
    entry.wrapper.hidden = false

    // `context` already contains LiteGraph's pan/zoom and node translation.
    // Mapping that exact transform into CSS avoids maintaining a second copy
    // of its coordinate maths and also follows HiDPI backing-store changes.
    const rect = this.surface.getBoundingClientRect()
    const ratioX = rect.width > 0 ? this.surface.width / rect.width : 1
    const ratioY = rect.height > 0 ? this.surface.height / rect.height : ratioX
    const transform = context.getTransform()
    const origin = new DOMPoint(WIDGET_MARGIN, y).matrixTransform(transform)
    entry.wrapper.style.width = `${Math.max(0, width - WIDGET_MARGIN * 2)}px`
    entry.wrapper.style.height = `${TEXTAREA_HEIGHT}px`
    entry.wrapper.style.transform = `matrix(${transform.a / ratioX}, ${
      transform.b / ratioY
    }, ${transform.c / ratioX}, ${transform.d / ratioY}, ${origin.x / ratioX}, ${
      origin.y / ratioY
    })`

    // setProperty() and graph restoration can update the widget without an
    // input event. Do not move the caret while the person is actively typing.
    if (document.activeElement !== entry.textarea && entry.textarea.value !== widget.value) {
      entry.textarea.value = widget.value
    }
  }

  finishFrame(): void {
    for (const entry of this.entries.values()) {
      if (entry.frame !== this.frame) {
        entry.wrapper.hidden = true
      }
    }
  }

  removeNode(node: LGraphNode): void {
    for (const [widget, entry] of this.entries) {
      if (entry.node === node) {
        entry.wrapper.remove()
        this.entries.delete(widget)
      }
    }
  }

  clear(): void {
    for (const entry of this.entries.values()) {
      entry.wrapper.remove()
    }
    this.entries.clear()
  }

  private createEntry(widget: TextareaWidget, node: LGraphNode): TextareaEntry {
    const wrapper = document.createElement('div')
    wrapper.className = 'hazor-node-textarea'

    const label = document.createElement('label')
    label.className = 'hazor-node-textarea__label'
    label.textContent = widget.options?.label ?? widget.name ?? ''

    const textarea = document.createElement('textarea')
    textarea.className = 'hazor-node-textarea__input'
    textarea.value = widget.value
    textarea.setAttribute('aria-label', label.textContent)
    textarea.wrap = 'soft'
    textarea.spellcheck = true

    wrapper.append(label, textarea)
    this.host.append(wrapper)

    textarea.addEventListener('input', () => {
      widget.value = textarea.value
      node.setProperty(widget.options?.property ?? '', textarea.value)
      node.graph?.change()
      node.setDirtyCanvas(true, false)
    })

    // LiteGraph listens on both the canvas and the document. Keep native text
    // editing events inside this control so Delete, Space and wheel scrolling
    // do not also delete a node, pan the graph or change its zoom.
    const stopPropagation = (event: Event): void => event.stopPropagation()
    for (const type of [
      'pointerdown',
      'pointerup',
      'mousedown',
      'mouseup',
      'click',
      'dblclick',
      'contextmenu',
      'keydown',
      'keyup',
      'keypress',
      'wheel',
    ]) {
      wrapper.addEventListener(type, stopPropagation)
    }

    const entry = { node, wrapper, textarea, frame: this.frame }
    this.entries.set(widget, entry)
    return entry
  }
}

/**
 * Installs the overlay lifetime hooks for one graph/canvas pair.
 *
 * The returned function restores any hooks that were present before this
 * manager and removes every DOM element it owns.
 */
export function installTextareaOverlay(
  graph: LGraph,
  canvas: LGraphCanvas,
  host: HTMLDivElement,
  surface: HTMLCanvasElement,
): () => void {
  const manager = new TextareaOverlayManager(host, surface)
  managers.set(graph, manager)

  const canvasHooks = canvas as unknown as CanvasHooks
  const graphHooks = graph as unknown as GraphHooks
  const previousRender = canvasHooks.onRender
  const previousForeground = canvasHooks.onDrawForeground
  const previousClear = canvasHooks.onClear
  const previousNodeRemoved = graphHooks.onNodeRemoved

  const render: RenderHook = (element, context) => {
    manager.beginFrame()
    previousRender?.call(canvas, element, context)
  }
  const foreground: ForegroundHook = (context, visibleArea) => {
    previousForeground?.call(canvas, context, visibleArea)
    manager.finishFrame()
  }
  const clear: ClearHook = () => {
    previousClear?.call(canvas)
    manager.clear()
  }
  const nodeRemoved = (node: LGraphNode): void => {
    previousNodeRemoved?.call(graph, node)
    manager.removeNode(node)
  }

  canvasHooks.onRender = render
  canvasHooks.onDrawForeground = foreground
  canvasHooks.onClear = clear
  graphHooks.onNodeRemoved = nodeRemoved

  return () => {
    manager.clear()
    managers.delete(graph)
    if (canvasHooks.onRender === render) {
      canvasHooks.onRender = previousRender
    }
    if (canvasHooks.onDrawForeground === foreground) {
      canvasHooks.onDrawForeground = previousForeground
    }
    if (canvasHooks.onClear === clear) {
      canvasHooks.onClear = previousClear
    }
    if (graphHooks.onNodeRemoved === nodeRemoved) {
      graphHooks.onNodeRemoved = previousNodeRemoved
    }
  }
}

/** Adds the node widget that reserves and updates one multiline editor. */
export function addTextareaWidget(
  node: LGraphNode,
  label: string,
  property: string,
  value: string,
): TextareaWidget {
  const widget: TextareaWidget = {
    hazorTextarea: true,
    name: label,
    value,
    options: { property, label },
    computeSize: (width) => [width, TEXTAREA_HEIGHT],
    draw: (context, owner, width, y) => {
      const graph = owner.graph
      if (graph) {
        managers.get(graph)?.layout(widget, owner, context, width, y)
      }
    },
  }
  return node.addCustomWidget(widget)
}
