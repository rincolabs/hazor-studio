/**
 * litegraph.js 0.7.x has a wider runtime surface than its bundled community
 * type definitions declare (see the `skipLibCheck` note in tsconfig.json —
 * those definitions are useful but not ours to fix). Every member below is
 * real at runtime; this is just where the cast that reaches it lives, so it
 * is reviewable in one place instead of re-declared per call site.
 */
import type { INodeInputSlot, INodeOutputSlot, IWidget, LGraphCanvas, LGraphNode, LLink, Vector2 } from 'litegraph.js'

/** Global constants assigned on the `LiteGraph` object. */
export interface UndeclaredLiteGraphConstants {
  NODE_SELECTED_TITLE_COLOR: string
  NODE_BOX_OUTLINE_COLOR: string
  WIDGET_BGCOLOR: string
  WIDGET_OUTLINE_COLOR: string
  WIDGET_TEXT_COLOR: string
  WIDGET_SECONDARY_TEXT_COLOR: string
  /** `"pointer"` or `"mouse"`, used to build the native event name it listens to. */
  pointerevents_method: string
  isValidConnection(typeA: string | number, typeB: string | number): boolean
}

/** Instance members of `LGraphCanvas`. */
export interface UndeclaredCanvasMembers {
  clear_background_color: string
  /** Declared `string`; litegraph only tests truthiness, so null turns its own grid off. */
  background_image: string | null
  default_connection_color_byType: Record<string, string>
  default_connection_color_byTypeOff: Record<string, string>
  /** Mouse position in graph coordinates, kept current by `adjustMouseEvent`. */
  graph_mouse: Vector2
  /** Set while a link is being dragged from an output/input, else `null`. */
  connecting_output: { type: string } | null
  connecting_input: { type: string } | null
  /** Routed by `installCanvasMenuRouting` to the Hazor canvas context menu. */
  hz_onCanvasMenu?: (event: MouseEvent) => void
  /** Routed by `installCanvasMenuRouting` to the Hazor Add Node menu. */
  hz_onSearch?: (event: MouseEvent) => void
}

/** The node list, kept private by the definitions although there is no public iterator. */
export interface GraphInternals {
  _nodes: LGraphNode[]
  /**
   * Bumped by every native mutator (add/remove a node, reconnect a link, …).
   * Declared `private`, but the type annotation is compile-time only — it is
   * a plain counter at runtime, and bumping it after a change this override
   * makes outside those mutators (a combo's value) is how the rest of
   * litegraph notices.
   */
  _version: number
}

/** `LLink`, plus the two fields the "flowing while running" effect needs. */
export type RunningLLink = LLink & { _last_time?: number; color?: string }

/**
 * The real signature has `ctx` first; the bundled definitions omit it
 * (confirmed against `litegraph.core.js`). A method declared with a missing
 * leading parameter cannot be widened back by a normal override, so this is
 * the actual signature cast through instead.
 */
export interface CorrectlyTypedRenderLink {
  renderLink(
    ctx: CanvasRenderingContext2D,
    a: Vector2,
    b: Vector2,
    link: LLink | null,
    skipBorder: boolean,
    flow: boolean,
    color?: string,
    startDir?: number,
    endDir?: number,
    numSublines?: number,
  ): void
}

/**
 * The real `activeWidget` is `IWidget | null` (the native call site passes
 * `null`, never `object`); the bundled definitions declare a bare `object`,
 * which rejects that `null`.
 */
export interface CorrectlyTypedDrawNodeWidgets {
  drawNodeWidgets(
    node: LGraphNode,
    posY: number,
    ctx: CanvasRenderingContext2D,
    activeWidget: IWidget | null,
  ): void
}

/**
 * The real signature also takes an `options` object (node/slot to connect
 * from, type filters, …); the bundled definitions declare only `event`.
 */
export interface CorrectlyTypedShowSearchBox {
  showSearchBox(event?: MouseEvent, options?: unknown): void
}

/**
 * `LGraphCanvas.active_canvas` is a static instance pointer kept up to date
 * by every instance method that runs (`LGraphCanvas.active_canvas = this`);
 * the bundled definitions mistake it for the `<canvas>` element.
 */
export interface StaticActiveCanvas {
  active_canvas: LGraphCanvas | null
}

/** What a widget's `draw` gets as a 6th argument from `installCanvasWidgets`'s own `drawNodeWidgets`. */
export interface WidgetDrawExtra {
  hover: boolean
  showText: boolean
  mouse: [number, number]
}

/**
 * `node.widgets` itself is missing from the bundled definitions (its
 * elements, `IWidget`, are declared). A custom widget's `draw` also takes one
 * more argument than `IWidget.draw` declares — the extra context above,
 * which only `installCanvasWidgets`'s own override ever passes.
 */
export interface HazorWidget extends IWidget {
  /** A fixed width narrower than the node, e.g. for a widget inside a sub-layout. */
  width?: number
  /** Drawn and skipped by click handling, but still laid out. */
  disabled?: boolean
  draw?(
    ctx: CanvasRenderingContext2D,
    node: LGraphNode,
    width: number,
    posY: number,
    height: number,
    extra?: WidgetDrawExtra,
  ): void
}

export interface UndeclaredNodeMembers {
  widgets?: HazorWidget[]
  /** Called by the native `processNodeWidgets` after any widget's value changes. */
  onWidgetChanged?: (name: string, value: unknown, previous: unknown, widget: HazorWidget) => void
  /**
   * Cached by the native `getConnectionPos` for a collapsed node
   * (`this._collapsed_width || LiteGraph.NODE_COLLAPSED_WIDTH`) — set it so a
   * link dragged from/to a collapsed node lands where it is actually drawn.
   */
  _collapsed_width?: number
}

export function nodeWidgets(node: LGraphNode): HazorWidget[] | undefined {
  return (node as unknown as UndeclaredNodeMembers).widgets
}

export function asNodeWithWidgetHook(node: LGraphNode): UndeclaredNodeMembers {
  return node as unknown as UndeclaredNodeMembers
}

/**
 * `slot.pos` is litegraph's own "hard-coded pos" override, read by
 * `getConnectionPos` before it falls back to computing a row from the slot
 * index — real, and how the layout below places inputs/outputs precisely
 * (and how a group label can sit between two of them), but missing from the
 * bundled definitions. `group`/`optional` are this editor's own convention,
 * passed through `addInput`'s `extra_info` the same undeclared way.
 */
export interface HazorSlotExtra extends Partial<INodeInputSlot> {
  pos?: Vector2
  group?: string
  optional?: boolean
  /** Stable backend port id; slot names may be numbered for variadic inputs. */
  hazorPortId?: string
  /** The backend port accepts an unbounded number of incoming links. */
  variadic?: boolean
}

export type HazorInputSlot = INodeInputSlot & HazorSlotExtra
export type HazorOutputSlot = INodeOutputSlot & HazorSlotExtra

export function asHazorInputSlot(slot: INodeInputSlot): HazorInputSlot {
  return slot as HazorInputSlot
}

export function asHazorOutputSlot(slot: INodeOutputSlot): HazorOutputSlot {
  return slot as HazorOutputSlot
}
