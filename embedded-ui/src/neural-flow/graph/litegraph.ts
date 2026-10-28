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
 */
import { LGraph, LGraphCanvas, LGraphNode, LiteGraph } from 'litegraph.js'
import 'litegraph.js/css/litegraph.css'

import type { ThemeVariables } from '../services/contract'

export { LGraph, LGraphCanvas, LGraphNode, LiteGraph }

/**
 * Constants litegraph.js has but its community type definitions do not
 * declare. Collected in one interface so the cast that reaches them is in one
 * place and the names are reviewable, instead of `any` sprinkled through the
 * theming below.
 */
interface UndeclaredConstants {
  NODE_SELECTED_TITLE_COLOR: string
  NODE_BOX_OUTLINE_COLOR: string
  WIDGET_BGCOLOR: string
  WIDGET_OUTLINE_COLOR: string
  WIDGET_TEXT_COLOR: string
  WIDGET_SECONDARY_TEXT_COLOR: string
}

/** Likewise for the canvas: `clear_background_color` and the per-type colours. */
interface UndeclaredCanvasMembers {
  clear_background_color: string
  default_connection_color_byType: Record<string, string>
  default_connection_color_byTypeOff: Record<string, string>
}

/** The node list, which the definitions keep private although there is no public iterator. */
interface GraphInternals {
  _nodes: LGraphNode[]
}

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

function colour(theme: ThemeVariables, key: string, fallback: string): string {
  const value = theme[key]
  return typeof value === 'string' && value.length > 0 ? value : fallback
}

/**
 * One colour per port type, so a MODEL link is told apart from a CONDITIONING
 * one at a glance.
 *
 * Taken from Hazor's theme rather than invented: these are semantic slots the
 * theme already defines, which is what keeps the editor in step with the rest
 * of the application when the theme changes. `danger` is deliberately not used
 * — red means an error everywhere else in Hazor.
 */
function portColours(theme: ThemeVariables, portTypes: string[]): Record<string, string> {
  const palette = [
    colour(theme, 'accent', '#4f8cc9'),
    colour(theme, 'success', '#5aa469'),
    colour(theme, 'warning', '#c79a3c'),
    colour(theme, 'borderFocus', '#5a8dd6'),
    colour(theme, 'textBright', '#f2f2f2'),
    colour(theme, 'textSecondary', '#9a9a9a'),
  ]
  const colours: Record<string, string> = {}
  portTypes.forEach((type, index) => {
    colours[type] = palette[index % palette.length] as string
  })
  return colours
}

/**
 * Dims a colour towards the panel background, for the hollow state of a port
 * that has nothing connected to it.
 */
function dim(value: string, background: string): string {
  // Only #rrggbb is produced by QColor::name(), so nothing more general is
  // needed; anything else is handed back untouched rather than mangled.
  const mix = (a: string, b: string): string | null => {
    const parse = (hex: string): [number, number, number] | null => {
      const match = /^#([0-9a-f]{6})$/i.exec(hex)
      if (!match) {
        return null
      }
      const n = Number.parseInt(match[1] as string, 16)
      return [(n >> 16) & 0xff, (n >> 8) & 0xff, n & 0xff]
    }
    const first = parse(a)
    const second = parse(b)
    if (!first || !second) {
      return null
    }
    const blended = first.map((channel, index) =>
      Math.round(channel * 0.45 + (second[index] as number) * 0.55),
    )
    return `#${blended.map((channel) => channel.toString(16).padStart(2, '0')).join('')}`
  }
  return mix(value, background) ?? value
}

/**
 * Applies Hazor's active theme to the node graph.
 *
 * Safe to call again when the theme changes: every value is assigned, none
 * accumulated.
 */
export function applyGraphTheme(
  canvasInstance: LGraphCanvas,
  theme: ThemeVariables,
  portTypes: string[],
): void {
  const background = colour(theme, 'background', '#1b1b1b')
  const surface = colour(theme, 'surface', '#252525')
  const surfaceDark = colour(theme, 'surfaceDark', '#1e1e1e')
  const border = colour(theme, 'border', '#3a3a3a')
  const text = colour(theme, 'text', '#d8d8d8')
  const textBright = colour(theme, 'textBright', '#f2f2f2')
  const textSecondary = colour(theme, 'textSecondary', '#9a9a9a')
  const accent = colour(theme, 'accent', '#4f8cc9')

  const constants = LiteGraph as unknown as UndeclaredConstants

  // Geometry: the defaults are built for short names like "value", and these
  // nodes carry labels like "Denoising strength".
  LiteGraph.NODE_WIDTH = 230
  LiteGraph.NODE_TITLE_HEIGHT = 26
  LiteGraph.NODE_TITLE_TEXT_Y = 18
  LiteGraph.NODE_SLOT_HEIGHT = 18
  LiteGraph.NODE_WIDGET_HEIGHT = 20
  LiteGraph.NODE_TEXT_SIZE = 12
  LiteGraph.NODE_SUBTEXT_SIZE = 11
  // NODE_DEFAULT_SHAPE is not set here: litegraph 0.7.18 declares it and never
  // reads it. drawNodeShape() falls back to ROUND_SHAPE on its own, which is
  // the shape wanted anyway, and assigning the constant would only look like
  // it were doing something.

  LiteGraph.NODE_DEFAULT_COLOR = surfaceDark
  LiteGraph.NODE_DEFAULT_BGCOLOR = surface
  LiteGraph.NODE_DEFAULT_BOXCOLOR = border
  LiteGraph.NODE_TITLE_COLOR = textSecondary
  LiteGraph.NODE_TEXT_COLOR = text
  constants.NODE_SELECTED_TITLE_COLOR = textBright
  constants.NODE_BOX_OUTLINE_COLOR = accent
  constants.WIDGET_BGCOLOR = surfaceDark
  constants.WIDGET_OUTLINE_COLOR = border
  constants.WIDGET_TEXT_COLOR = text
  constants.WIDGET_SECONDARY_TEXT_COLOR = textSecondary
  // Flat, like the rest of Hazor's panels. The library's drop shadow is what
  // makes a default litegraph canvas look like a different application.
  LiteGraph.DEFAULT_SHADOW_COLOR = 'rgba(0, 0, 0, 0)'

  const canvasMembers = canvasInstance as unknown as UndeclaredCanvasMembers
  canvasMembers.clear_background_color = background
  canvasInstance.render_shadows = false
  canvasInstance.render_canvas_border = false
  canvasInstance.round_radius = 5
  canvasInstance.connections_width = 2
  canvasInstance.links_render_mode = LiteGraph.SPLINE_LINK
  canvasInstance.default_link_color = textSecondary
  LiteGraph.LINK_COLOR = textSecondary
  LiteGraph.CONNECTING_LINK_COLOR = accent

  const colours = portColours(theme, portTypes)
  canvasMembers.default_connection_color_byType = { ...colours }
  canvasMembers.default_connection_color_byTypeOff = Object.fromEntries(
    Object.entries(colours).map(([type, value]) => [type, dim(value, background)]),
  )
  canvasInstance.default_connection_color = {
    input_off: border,
    input_on: accent,
    output_off: border,
    output_on: accent,
  }
  for (const [type, value] of Object.entries(colours)) {
    LGraphCanvas.link_type_colors[type] = value
  }

  canvasInstance.setDirty(true, true)
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
 * corner of the page. This one has a header above it and a palette beside it.
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
  const graph = new LGraph()
  const canvasInstance = new LGraphCanvas(element, graph)

  canvasInstance.allow_dragcanvas = true
  canvasInstance.allow_dragnodes = true
  // The search box adds its own node browser, built around the demo library's
  // naming. The palette and the context menu are the two ways in here.
  canvasInstance.allow_searchbox = false
  canvasInstance.highquality_render = true

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
  // Never started — there is no inference in the browser — but stopping an
  // execution loop that was never running is free, and this is the method that
  // would have to be called if one ever were.
  graph.stop()
  graph.clear()
}
