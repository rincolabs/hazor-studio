/**
 * The one place litegraph.js is imported, and the one place its look is set.
 *
 * The library is the original jagenjo/litegraph.js, pinned in package.json and
 * resolved to its `litegraph.core.js` bundle by `vite.config.ts` — the editor
 * without the demo node library, so the only node types in the "Add node" menu
 * are Hazor's.
 *
 * Its job here is presentation and interaction with the graph, nothing else:
 * the execution loop (`graph.start()` / `runStep()`) is never started, because
 * no inference happens in the browser.
 *
 * The look itself — node frame, widgets, the grid — is the "B — Tinted" style
 * from the approved Neural Flow design, static like the rest of the shared
 * web design system (see AGENTS.md on the native `Theme` vs. this one).
 */
import { LGraph, LGraphCanvas, LGraphNode, LiteGraph } from 'litegraph.js'
import 'litegraph.js/css/litegraph.css'
import './litegraph-theme.css'

import { installCanvasMenuRouting } from './canvasMenuBridge'
import { installCanvasRenderer } from './canvasRenderer'
import { SOCKET_COLORS, T } from './canvasTheme'
import { installCanvasWidgets } from './canvasWidgets'
import type { GraphInternals, UndeclaredCanvasMembers, UndeclaredLiteGraphConstants } from './litegraphInternals'

export { LGraph, LGraphCanvas, LGraphNode, LiteGraph }

export function graphNodes(graph: LGraph): LGraphNode[] {
  return (graph as unknown as GraphInternals)._nodes ?? []
}

/** How much one wheel notch zooms. litegraph's own figure. */
const ZOOM_STEP = 1.1

/**
 * Upper bound on the backing-store scale.
 *
 * The canvas is allocated at the screen's pixel density so it is not upscaled
 * by the compositor, and a large floating panel on a 4x display would
 * otherwise mean a very large bitmap for very little visible gain.
 */
const MAX_PIXEL_RATIO = 3

let installed = false

/**
 * Global constants and the renderer/widget/menu overrides. Idempotent and
 * called once, before the first canvas is created, so every `LGraphCanvas`
 * instance (there is only ever one, but a dev-server HMR reload can still ask
 * twice) picks up the same look.
 */
function installHazorLook(): void {
  if (installed) return
  installed = true

  LiteGraph.NODE_SLOT_HEIGHT = T.rowH
  LiteGraph.NODE_WIDGET_HEIGHT = T.rowH
  LiteGraph.NODE_TEXT_SIZE = 12
  LiteGraph.NODE_SUBTEXT_SIZE = 11
  LiteGraph.NODE_TITLE_HEIGHT = T.titleH
  LiteGraph.NODE_TITLE_TEXT_Y = 21
  LiteGraph.NODE_TITLE_COLOR = T.text
  LiteGraph.NODE_TEXT_COLOR = T.text2
  LiteGraph.NODE_DEFAULT_COLOR = T.nodeHead
  LiteGraph.NODE_DEFAULT_BGCOLOR = T.nodeBg
  LiteGraph.NODE_DEFAULT_BOXCOLOR = T.text3
  LiteGraph.LINK_COLOR = '#8a8d8f'
  LiteGraph.CONNECTING_LINK_COLOR = T.accentText
  // Flat, like the rest of Hazor's panels — `drawNodeShape` paints its own
  // shadow directly, conditioned on `render_shadows` and zoom.
  LiteGraph.DEFAULT_SHADOW_COLOR = T.shadow

  const constants = LiteGraph as unknown as UndeclaredLiteGraphConstants
  constants.NODE_SELECTED_TITLE_COLOR = T.text
  constants.NODE_BOX_OUTLINE_COLOR = T.accent
  constants.WIDGET_BGCOLOR = T.widgetBg
  constants.WIDGET_OUTLINE_COLOR = T.widgetBorder
  constants.WIDGET_TEXT_COLOR = T.text
  constants.WIDGET_SECONDARY_TEXT_COLOR = T.text3

  Object.assign(LGraphCanvas.link_type_colors, SOCKET_COLORS)

  installCanvasRenderer()
  installCanvasWidgets()
  installCanvasMenuRouting()
}

/**
 * Per-canvas settings and the grid background. Safe to call more than once;
 * every value is assigned, none accumulated.
 */
function themeCanvas(canvasInstance: LGraphCanvas): void {
  canvasInstance.title_text_font = `600 12px ${T.font}`
  canvasInstance.inner_text_font = `400 11.5px ${T.font}`
  canvasInstance.node_title_color = T.text
  canvasInstance.default_link_color = '#8a8d8f'
  canvasInstance.render_shadows = true
  canvasInstance.render_canvas_border = false
  canvasInstance.render_connections_border = true
  canvasInstance.render_connection_arrows = false
  canvasInstance.render_curved_connections = true
  canvasInstance.connections_width = 2
  canvasInstance.round_radius = T.radius
  canvasInstance.render_title_colored = true
  // In 0.7.x this flag only controls the arrow/centre dot drawn on each link.
  // Off = clean wires; clicking the middle of a link still opens its menu.
  canvasInstance.highquality_render = false
  canvasInstance.show_info = false // hides the "T: / I: / N:" debug text
  canvasInstance.links_render_mode = LiteGraph.SPLINE_LINK

  const canvasMembers = canvasInstance as unknown as UndeclaredCanvasMembers
  canvasMembers.clear_background_color = T.wsBg
  // Off, or litegraph tiles its own grid image under the one drawn below.
  canvasMembers.background_image = null
  canvasMembers.default_connection_color_byType = { ...SOCKET_COLORS }
  canvasMembers.default_connection_color_byTypeOff = { ...SOCKET_COLORS }
  canvasInstance.default_connection_color = {
    input_off: T.nodeBorder,
    input_on: T.accent,
    output_off: T.nodeBorder,
    output_on: T.accent,
  }

  // Grid: 20px minor, 100px major, following pan/zoom. Replaces litegraph's
  // own background image, which this theme does not set.
  canvasInstance.onDrawBackground = function (this: LGraphCanvas, ctx, visibleArea) {
    const [x0, y0, w, h] = visibleArea
    const scale = this.ds.scale
    const draw = (step: number, color: string): void => {
      ctx.strokeStyle = color
      ctx.lineWidth = 1 / scale
      ctx.beginPath()
      for (let x = Math.floor(x0 / step) * step; x < x0 + w; x += step) {
        ctx.moveTo(x, y0)
        ctx.lineTo(x, y0 + h)
      }
      for (let y = Math.floor(y0 / step) * step; y < y0 + h; y += step) {
        ctx.moveTo(x0, y)
        ctx.lineTo(x0 + w, y)
      }
      ctx.stroke()
    }
    if (scale > 0.45) draw(20, T.gridMinor)
    draw(100, T.gridMajor)
  }
}

/**
 * Makes litegraph's pointer maths agree with a backing store that is not the
 * element's CSS size, and zoom follow the cursor.
 *
 * litegraph 0.7 assumes a 1:1 canvas: `adjustMouseEvent` subtracts the
 * element's origin, divides by `ds.scale` and is done, so a canvas allocated
 * at the screen's pixel density would be drawn crisply and clicked in the
 * wrong place. Everything the library draws is in backing-store pixels, so the
 * pointer is converted into them here.
 *
 * The wheel handler goes with it for two reasons: the same conversion, and
 * because upstream anchors the zoom at the event's raw viewport coordinates,
 * which are the canvas's own only when the canvas starts at the top-left
 * corner of the page. This one has a toolbar above it.
 */
function installPointerCorrections(
  canvasInstance: LGraphCanvas,
  element: HTMLCanvasElement,
): void {
  const processContextMenu = canvasInstance.processContextMenu
  canvasInstance.processContextMenu = function (node, event): void {
    const mouseEvent = event as MouseEvent
    // LiteGraph 0.7.18 routes `pointer_is_double` through the same branch as a
    // right click (`e.which == 3 || this.pointer_is_double`). When that state
    // remains set after a missed mouse-up, an ordinary left click opens the
    // node menu. The menu itself has one unambiguous contract: it opens only
    // for the secondary mouse button.
    if (mouseEvent.button !== 2 && mouseEvent.which !== 3) {
      return
    }
    processContextMenu.call(this, node, event)
  }

  const toBackingStore = (event: MouseEvent): [number, number] => {
    const rect = element.getBoundingClientRect()
    const ratio = rect.width > 0 ? element.width / rect.width : 1
    return [(event.clientX - rect.left) * ratio, (event.clientY - rect.top) * ratio]
  }

  canvasInstance.adjustMouseEvent = function (event: MouseEvent): void {
    const [x, y] = toBackingStore(event)
    // Written but never read by this version of the library; kept in step so
    // it does not become a trap for whoever reads it next.
    this.last_mouse_position[0] = x
    this.last_mouse_position[1] = y
    const positioned = event as MouseEvent & { canvasX: number; canvasY: number }
    positioned.canvasX = x / this.ds.scale - this.ds.offset[0]
    positioned.canvasY = y / this.ds.scale - this.ds.offset[1]
  }

  canvasInstance.processMouseWheel = function (event: MouseEvent): boolean | undefined {
    if (!this.graph || !this.allow_dragcanvas) {
      return undefined
    }
    const wheel = event as WheelEvent & { wheelDeltaY?: number; detail: number }
    const delta = wheel.wheelDeltaY != null ? wheel.wheelDeltaY : wheel.detail * -60
    if (delta === 0) {
      return undefined
    }
    this.ds.changeScale(
      delta > 0 ? this.ds.scale * ZOOM_STEP : this.ds.scale / ZOOM_STEP,
      toBackingStore(event),
    )
    this.graph.change()
    event.preventDefault()
    return false
  }
}

/**
 * Creates the graph and its canvas, bound to `element`.
 *
 * The caller owns the lifetime: `releaseGraphCanvas` must be called before the
 * element goes away, because litegraph binds listeners outside it.
 */
export function createGraphCanvas(element: HTMLCanvasElement): {
  graph: LGraph
  canvas: LGraphCanvas
} {
  installHazorLook()

  const graph = new LGraph()
  const canvasInstance = new LGraphCanvas(element, graph)
  themeCanvas(canvasInstance)

  canvasInstance.allow_dragcanvas = true
  canvasInstance.allow_dragnodes = true
  canvasInstance.allow_searchbox = true

  // The corrections replace two methods, and the constructor has already
  // registered a *bound* reference to the original wheel handler
  // (`bindEvents()` stores `this.processMouseWheel.bind(this)`), so a
  // replacement installed afterwards would never be the one that runs.
  // Rebinding is what installs it.
  //
  // The constructor's `skip_events` option would have prevented the first
  // binding, but it is also what tells `processMouseDown` to follow a drag off
  // the canvas onto the document — so it is not the lever to use here.
  canvasInstance.unbindEvents()
  installPointerCorrections(canvasInstance, element)
  canvasInstance.bindEvents()

  return { graph, canvas: canvasInstance }
}

/**
 * Sizes the drawing surface to the element's box at the screen's pixel
 * density, and keeps the zoom looking the same across a density change.
 *
 * Returns the ratio it used so the caller can keep it.
 */
export function resizeGraphCanvas(
  canvasInstance: LGraphCanvas,
  element: HTMLCanvasElement,
  previousRatio: number,
): number {
  const width = element.clientWidth
  const height = element.clientHeight
  if (width <= 0 || height <= 0) {
    return previousRatio
  }

  const ratio = Math.min(Math.max(window.devicePixelRatio || 1, 1), MAX_PIXEL_RATIO)
  canvasInstance.resize(Math.round(width * ratio), Math.round(height * ratio))

  if (ratio !== previousRatio) {
    // ds.scale is in backing-store pixels per graph unit, so the user's zoom
    // has to follow the density to stay visually the same — including the
    // first call, which is what makes 100% zoom crisp rather than tiny.
    canvasInstance.ds.scale *= ratio / previousRatio
    canvasInstance.setDirty(true, true)
  }
  return ratio
}

/** Unbinds everything litegraph attached outside the canvas element. */
export function releaseGraphCanvas(canvasInstance: LGraphCanvas, graph: LGraph): void {
  // litegraph listens on the document as well as on the canvas, so dropping
  // the element is not enough to stop it receiving events.
  canvasInstance.unbindEvents()
  // The constructor started a requestAnimationFrame loop; it outlives the element.
  canvasInstance.stopRendering()
  // Never started — there is no inference in the browser — but stopping an
  // execution loop that was never running is free, and this is the method that
  // would have to be called if one ever were.
  graph.stop()
  graph.clear()
}
