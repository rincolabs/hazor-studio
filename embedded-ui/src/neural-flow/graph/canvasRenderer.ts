/**
 * Hazor node renderer for the original LiteGraph (jagenjo/litegraph.js 0.7.x).
 *
 * Replaces `LGraphCanvas.prototype.drawNode` and `drawNodeShape` with the
 * "B — Tinted" node style: a category-tinted header, so node types stay
 * recognisable at low zoom, over an otherwise neutral body.
 *
 * Everything else (input handling, dragging, links, serialisation) stays
 * native. Hit-testing keeps working because slots are drawn at
 * `node.getConnectionPos()` and the title height stays in
 * `LiteGraph.NODE_TITLE_HEIGHT` — nothing here changes node geometry, only
 * how it is painted.
 */
import { LGraphCanvas, LiteGraph } from 'litegraph.js'
import type { INodeInputSlot, INodeOutputSlot, LGraphNode } from 'litegraph.js'

import { CATEGORY_COLORS, CATEGORY_ICONS, ICON_PATHS, SOCKET_COLORS, T } from './canvasTheme'
import type {
  CorrectlyTypedDrawNodeWidgets,
  CorrectlyTypedRenderLink,
  RunningLLink,
  UndeclaredCanvasMembers,
  UndeclaredLiteGraphConstants,
  UndeclaredNodeMembers,
} from './litegraphInternals'
import { asHazorInputSlot, asHazorOutputSlot, nodeWidgets } from './litegraphInternals'
import type { HazorVisualNode, NodeVisualState } from './nodeVisualState'

const icons: Record<string, Path2D[]> = {}

function icon(name: string): Path2D[] {
  let paths = icons[name]
  if (!paths) {
    paths = (ICON_PATHS[name] ?? []).map((d) => new Path2D(d))
    icons[name] = paths
  }
  return paths
}

/** Strokes one of the 24x24 icons at (x, y) with the given size. */
export function drawIcon(
  ctx: CanvasRenderingContext2D,
  name: string,
  x: number,
  y: number,
  size: number,
  color: string,
  lineWidth = 1.8,
): void {
  ctx.save()
  ctx.translate(x, y)
  ctx.scale(size / 24, size / 24)
  ctx.strokeStyle = color
  ctx.lineWidth = lineWidth
  ctx.lineCap = 'round'
  ctx.lineJoin = 'round'
  for (const path of icon(name)) ctx.stroke(path)
  ctx.restore()
}

export function roundRect(
  ctx: CanvasRenderingContext2D,
  x: number,
  y: number,
  w: number,
  h: number,
  r: number | number[],
): void {
  ctx.beginPath()
  ctx.roundRect(x, y, w, h, r)
}

/** Truncates text with an ellipsis to fit maxW (uses the current ctx.font). */
export function fitText(ctx: CanvasRenderingContext2D, text: unknown, maxW: number): string {
  const full = String(text)
  if (ctx.measureText(full).width <= maxW) return full
  let lo = 0
  let hi = full.length
  while (lo < hi) {
    const mid = (lo + hi + 1) >> 1
    if (ctx.measureText(`${full.slice(0, mid)}…`).width <= maxW) lo = mid
    else hi = mid - 1
  }
  return `${full.slice(0, lo)}…`
}

export function font(size: number, weight = 400, family: string = T.font): string {
  return `${weight} ${size}px ${family}`
}

function visualState(node: LGraphNode): NodeVisualState | undefined {
  return (node as Partial<HazorVisualNode>).hz
}

function category(node: LGraphNode): { id: string; color: string; tint: string } {
  const id = visualState(node)?.category ?? 'output'
  const [color, tint] = CATEGORY_COLORS[id] ?? (CATEGORY_COLORS.output as readonly [string, string])
  return { id, color, tint }
}

function badgeText(node: LGraphNode): string {
  const hz = visualState(node)
  if (hz?.state === 'running') return hz.progressLabel || `${Math.round((hz.progress || 0) * 100)}%`
  if (hz?.state === 'error') return 'Error'
  return ''
}

function badgeWidth(ctx: CanvasRenderingContext2D, node: LGraphNode): number {
  const text = badgeText(node)
  if (!text) return 0
  ctx.font = font(10, 600)
  return ctx.measureText(text).width + 12 + 8
}

function drawBadge(ctx: CanvasRenderingContext2D, node: LGraphNode, w: number, cy: number): void {
  const text = badgeText(node)
  if (!text) return
  ctx.font = font(10, 600)
  const bw = ctx.measureText(text).width + 12
  const x = w - 8 - bw
  const isError = visualState(node)?.state === 'error'
  ctx.fillStyle = isError ? T.errorBadgeBg : T.infoBg
  roundRect(ctx, x, cy - 8, bw, 16, 8)
  ctx.fill()
  ctx.fillStyle = isError ? T.errorIcon : T.info
  ctx.textAlign = 'center'
  ctx.fillText(text, x + bw / 2, cy + 3.5)
  ctx.textAlign = 'left'
}

function drawSocket(
  ctx: CanvasRenderingContext2D,
  x: number,
  y: number,
  color: string,
  hollow: boolean,
  low: boolean,
): void {
  const r = T.socketR
  // 2px ring in the node body colour so sockets read cleanly over wires and borders.
  ctx.fillStyle = T.nodeBg
  ctx.beginPath()
  if (low) ctx.rect(x - r - 2, y - r - 2, (r + 2) * 2, (r + 2) * 2)
  else ctx.arc(x, y, r + 2, 0, Math.PI * 2)
  ctx.fill()
  ctx.beginPath()
  if (low) ctx.rect(x - r, y - r, r * 2, r * 2)
  else ctx.arc(x, y, r, 0, Math.PI * 2)
  if (hollow) {
    ctx.strokeStyle = color
    ctx.lineWidth = 2
    ctx.beginPath()
    if (low) ctx.rect(x - r + 1, y - r + 1, r * 2 - 2, r * 2 - 2)
    else ctx.arc(x, y, r - 1, 0, Math.PI * 2)
    ctx.stroke()
  } else {
    ctx.fillStyle = color
    ctx.fill()
  }
}

function drawCollapsedSlots(ctx: CanvasRenderingContext2D, node: LGraphNode, w: number, th: number): void {
  const input = node.inputs?.find((slot) => slot.link != null)
  const output = node.outputs?.find((slot) => slot.links && slot.links.length)
  if (input) drawSocket(ctx, 0, -th / 2, SOCKET_COLORS[input.type as string] ?? '#999', false, false)
  if (output) drawSocket(ctx, w, -th / 2, SOCKET_COLORS[output.type as string] ?? '#999', false, false)
}

function drawErrorFooter(ctx: CanvasRenderingContext2D, node: LGraphNode, error: string): void {
  const w = node.size[0]
  const y = node.size[1] - 6 - 30
  ctx.fillStyle = T.errorBg
  roundRect(ctx, 6, y, w - 12, 30, 3)
  ctx.fill()
  drawIcon(ctx, 'alert', 13, y + 9, 12, T.errorIcon)
  ctx.font = font(11)
  ctx.fillStyle = T.errorText
  ctx.textBaseline = 'middle'
  ctx.fillText(fitText(ctx, error, w - 46), 31, y + 15.5)
  ctx.textBaseline = 'alphabetic'
}

/** Installs the Hazor node and link renderer. Call once, before any canvas draws. */
export function installCanvasRenderer(): void {
  LGraphCanvas.prototype.drawNodeShape = function (
    node,
    ctx,
    size,
    _fgcolor,
    _bgcolor,
    selected,
    mouseOver,
  ): void {
    const th = LiteGraph.NODE_TITLE_HEIGHT
    const low = this.ds.scale < 0.5
    const collapsed = Boolean(node.flags.collapsed)
    const w = size[0]
    const h = collapsed ? 0 : size[1]
    const r = T.radius
    const cat = category(node)
    const hz = visualState(node)
    const state = hz?.state ?? 'default'

    // body (whole node incl. header)
    ctx.fillStyle = T.nodeBg
    roundRect(ctx, 0, -th, w, h + th, low ? 0 : r)
    ctx.fill()
    ctx.shadowColor = 'transparent'

    // header, tinted by category
    ctx.fillStyle = cat.tint
    roundRect(ctx, 0, -th, w, th, low ? 0 : collapsed ? r : [r, r, 0, 0])
    ctx.fill()
    if (!collapsed) {
      ctx.fillStyle = 'rgba(0,0,0,0.28)'
      ctx.fillRect(0, -1, w, 1)
    }

    node.onDrawBackground?.(ctx, this.canvas)

    if (!low) {
      // collapse chevron (litegraph collapses on a click in the first title-height square)
      const cy = -th / 2
      ctx.save()
      ctx.translate(14, cy)
      if (collapsed) ctx.rotate(-Math.PI / 2)
      drawIcon(ctx, 'chevronDown', -6, -6, 12, 'rgba(255,255,255,0.7)')
      ctx.restore()

      // category icon
      drawIcon(ctx, CATEGORY_ICONS[cat.id] ?? 'cube', 26, cy - 6.5, 13, cat.color)

      // title + mono subtitle (e.g. the loaded model)
      const right = badgeWidth(ctx, node) + 8
      const tx = 46
      const maxW = w - tx - right
      const subtitle = !collapsed ? hz?.subtitle : ''
      ctx.textAlign = 'left'
      ctx.textBaseline = 'alphabetic'
      ctx.font = font(12, 600)
      ctx.fillStyle = T.text
      const title = node.getTitle()
      if (subtitle) {
        ctx.fillText(fitText(ctx, title, maxW), tx, cy - 1)
        ctx.font = font(10.5, 400, T.mono)
        ctx.fillStyle = 'rgba(255,255,255,0.62)'
        ctx.fillText(fitText(ctx, subtitle, maxW), tx, cy + 12)
      } else {
        ctx.fillText(fitText(ctx, title, maxW), tx, cy + 4)
      }
      drawBadge(ctx, node, w, cy)
    }

    // progress bar under the header while running
    if (state === 'running' && !collapsed) {
      ctx.fillStyle = 'rgba(255,255,255,0.06)'
      ctx.fillRect(0, 0, w, 2)
      ctx.fillStyle = T.accentText
      ctx.fillRect(0, 0, w * Math.max(0, Math.min(1, hz?.progress ?? 0)), 2)
    }

    // border by state
    let stroke: string = T.nodeBorder
    let ring: string | null = null
    if (state === 'error') {
      stroke = T.error
      ring = 'rgba(196,85,75,0.45)'
    } else if (selected) {
      stroke = T.accent
      ring = T.accent
    } else if (state === 'running') {
      stroke = T.running
    } else if (mouseOver) {
      stroke = T.nodeBorderHover
    }
    if (ring && !low) {
      ctx.strokeStyle = ring
      ctx.lineWidth = 3
      roundRect(ctx, -1, -th - 1, w + 2, h + th + 2, r + 1)
      ctx.stroke()
    }
    ctx.strokeStyle = stroke
    ctx.lineWidth = 1
    roundRect(ctx, 0.5, -th + 0.5, w - 1, h + th - 1, r)
    ctx.stroke()
  }

  const slotLabel = (slot: INodeInputSlot | INodeOutputSlot): string | undefined =>
    slot.label ?? slot.name

  LGraphCanvas.prototype.drawNode = function (node, ctx): void {
    this.current_node = node
    const low = this.ds.scale < 0.6
    const th = LiteGraph.NODE_TITLE_HEIGHT

    if (this.live_mode) {
      if (!node.flags.collapsed) node.onDrawForeground?.(ctx, this.canvas)
      return
    }
    ctx.globalAlpha = this.editor_alpha

    if (this.render_shadows && !low) {
      ctx.shadowColor = T.shadow
      ctx.shadowOffsetX = 0
      ctx.shadowOffsetY = 4 * this.ds.scale
      ctx.shadowBlur = 14 * this.ds.scale
    } else {
      ctx.shadowColor = 'transparent'
    }

    const size: [number, number] = [node.size[0], node.size[1]]
    if (node.flags.collapsed) {
      ctx.font = font(12, 600)
      const collapsedWidth = Math.min(node.size[0], ctx.measureText(node.getTitle()).width + 46 + 16)
      size[0] = collapsedWidth
      size[1] = 0
      // Read by the native getConnectionPos so a link dragged from/to this
      // node lands where it is actually drawn while collapsed.
      ;(node as unknown as UndeclaredNodeMembers)._collapsed_width = collapsedWidth
    }

    this.drawNodeShape(node, ctx, size, node.color, node.bgcolor, node.is_selected, node.mouseOver)
    ctx.shadowColor = 'transparent'

    node.onDrawForeground?.(ctx, this.canvas)

    if (node.flags.collapsed) {
      drawCollapsedSlots(ctx, node, size[0], th)
      ctx.globalAlpha = 1
      return
    }

    // group labels ("Optional · LoRA")
    const hz = visualState(node)
    if (!low && hz?.groups.length) {
      ctx.font = font(9.5, 600)
      ctx.letterSpacing = '0.6px'
      for (const group of hz.groups) {
        ctx.fillStyle = T.text3
        ctx.textAlign = 'left'
        const label = group.text.toUpperCase()
        ctx.fillText(label, T.padX, group.y)
        const labelWidth = ctx.measureText(label).width
        ctx.fillStyle = 'rgba(255,255,255,0.07)'
        ctx.fillRect(T.padX + labelWidth + 8, group.y - 3, node.size[0] - T.padX * 2 - labelWidth - 8, 1)
      }
      ctx.letterSpacing = '0px'
    }

    // slots
    let maxY = 0
    const dragOut = (this as unknown as UndeclaredCanvasMembers).connecting_output
    const dragIn = (this as unknown as UndeclaredCanvasMembers).connecting_input
    const isValidConnection = (LiteGraph as unknown as UndeclaredLiteGraphConstants).isValidConnection
    ctx.font = font(11.5)
    ctx.textBaseline = 'middle'
    const drawSlot = (slot: INodeInputSlot | INodeOutputSlot, index: number, isInput: boolean): void => {
      const pos = node.getConnectionPos(isInput, index)
      const x = pos[0] - node.pos[0]
      const y = pos[1] - node.pos[1]
      maxY = Math.max(maxY, y + T.rowH / 2)
      const other = isInput ? dragOut : dragIn
      ctx.globalAlpha =
        other && !isValidConnection(slot.type, other.type) ? 0.4 * this.editor_alpha : this.editor_alpha
      const connected = isInput
        ? asHazorInputSlot(slot as INodeInputSlot).link != null
        : Boolean(asHazorOutputSlot(slot as INodeOutputSlot).links?.length)
      const optional = Boolean(asHazorInputSlot(slot as INodeInputSlot).optional)
      drawSocket(ctx, x, y, SOCKET_COLORS[slot.type as string] ?? '#999', optional && !connected, low)
      if (!low) {
        const text = slotLabel(slot)
        if (text) {
          ctx.fillStyle = optional ? T.text3 : T.text2
          ctx.textAlign = isInput ? 'left' : 'right'
          ctx.fillText(text, isInput ? x + 12 : x - 12, y + 0.5)
        }
      }
    }
    node.inputs?.forEach((slot, index) => drawSlot(slot, index, true))
    node.outputs?.forEach((slot, index) => drawSlot(slot, index, false))
    ctx.globalAlpha = this.editor_alpha
    ctx.textBaseline = 'alphabetic'
    ctx.textAlign = 'left'

    // widgets
    if (nodeWidgets(node)?.length) {
      const y0 = node.widgets_start_y ?? maxY
      const activeWidget = this.node_widget && this.node_widget[0] === node ? this.node_widget[1] : null
      ;(this as unknown as CorrectlyTypedDrawNodeWidgets).drawNodeWidgets(node, y0, ctx, activeWidget)
    }

    // error footer
    if (hz?.state === 'error' && hz.error && !low) drawErrorFooter(ctx, node, hz.error)

    // running: keep the incoming links "flowing" (native litegraph flow effect)
    if (hz?.state === 'running' && node.graph) {
      const now = LiteGraph.getTime()
      for (const input of node.inputs ?? []) {
        const link = input.link != null ? (node.graph.links[input.link] as RunningLLink) : undefined
        if (link) link._last_time = now
      }
      this.dirty_canvas = true
      // Links live on the background canvas, so the flow only advances if that
      // one is redrawn too — otherwise it moves just when something else does.
      this.dirty_bgcanvas = true
    }
    ctx.globalAlpha = 1
  }

  // Links of the selected node keep their type colour and get thicker.
  // Native litegraph paints a highlighted link white; this keeps the colour.
  const canvasProto = LGraphCanvas.prototype as unknown as CorrectlyTypedRenderLink
  const nativeRenderLink = canvasProto.renderLink
  canvasProto.renderLink = function (this: LGraphCanvas, ctx, a, b, link, skipBorder, flow, color, ...rest) {
    // The running "flow" dots: the wire's own colour, not litegraph's white.
    if (flow && link) {
      color =
        (link as RunningLLink).color || LGraphCanvas.link_type_colors[link.type as string] || color
    }
    if (link && this.highlighted_links[link.id] && !flow) {
      const width = this.connections_width
      delete this.highlighted_links[link.id]
      this.connections_width = width + 1
      try {
        nativeRenderLink.call(this, ctx, a, b, link, skipBorder, flow, color, ...rest)
        return
      } finally {
        this.connections_width = width
        this.highlighted_links[link.id] = true
      }
    }
    nativeRenderLink.call(this, ctx, a, b, link, skipBorder, flow, color, ...rest)
  }
}
