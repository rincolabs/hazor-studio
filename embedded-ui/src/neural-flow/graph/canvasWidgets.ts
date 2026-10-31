/**
 * Hazor-style drawing for litegraph's native widgets, for the original
 * LiteGraph (jagenjo/litegraph.js 0.7.x).
 *
 * `drawNodeWidgets` is replaced outright, but geometry stays compatible with
 * litegraph's own `processNodeWidgets` (hit area x 6…w-12, arrow zones
 * x < 40 / x > w-40), so native interaction — drag to scrub a number,
 * double-click to type, click to toggle — keeps working unchanged. Only the
 * combo widget's click is wrapped, to open the Hazor popup menu (the Add Node
 * menu's own `MenuHost`) instead of litegraph's `ContextMenu`.
 *
 * The seed widget is the one addition: a plain number plus a "randomise each
 * run" toggle, added via `addCustomWidget`. It stores its own `random` flag
 * and reduces to the same numeric property either way — the backend sees one
 * seed value, exactly as it does today.
 */
import { LGraphCanvas, LiteGraph } from 'litegraph.js'
import type { IWidget, LGraphNode, Vector2 } from 'litegraph.js'

import { drawIcon, fitText, font, roundRect } from './canvasRenderer'
import { T } from './canvasTheme'
import type {
  GraphInternals,
  HazorWidget,
  StaticActiveCanvas,
  UndeclaredCanvasMembers,
  UndeclaredLiteGraphConstants,
} from './litegraphInternals'
import { asNodeWithWidgetHook, nodeWidgets } from './litegraphInternals'

export interface MenuPopupItem {
  label: string
  checked: boolean
  action: () => void
}

export interface MenuPopupOptions {
  x: number
  y: number
  minWidth: number
  items: MenuPopupItem[]
}

/** Set by whoever owns the Vue menu system before the first click. */
export const widgetHooks: { openMenu: ((options: MenuPopupOptions) => void) | null } = {
  openMenu: null,
}

const M = T.padX // widget side margin inside the node
const H = T.rowH

/** Graph-space rect -> client (screen) rect, for a popup anchored to a widget. */
export function graphRectToClient(
  canvas: LGraphCanvas,
  node: LGraphNode,
  x: number,
  y: number,
  w: number,
  h: number,
): { left: number; top: number; width: number; height: number } {
  const bounds = canvas.canvas.getBoundingClientRect()
  // ds.scale counts backing-store pixels; a client rect is in CSS pixels.
  const ratio = bounds.width > 0 ? canvas.canvas.width / bounds.width : 1
  const scale = canvas.ds.scale / ratio
  const offset = canvas.ds.offset
  return {
    left: bounds.left + (node.pos[0] + x + offset[0]) * scale,
    top: bounds.top + (node.pos[1] + y + offset[1]) * scale,
    width: w * scale,
    height: h * scale,
  }
}

function box(
  ctx: CanvasRenderingContext2D,
  x: number,
  y: number,
  w: number,
  h: number,
  hover: boolean,
  disabled = false,
): void {
  const lit = hover && !disabled
  ctx.fillStyle = lit ? T.widgetBgHover : T.widgetBg
  roundRect(ctx, x, y, w, h, T.widgetR)
  ctx.fill()
  ctx.strokeStyle = lit ? T.widgetBorderHover : T.widgetBorder
  ctx.lineWidth = 1
  roundRect(ctx, x + 0.5, y + 0.5, w - 1, h - 1, T.widgetR)
  ctx.stroke()
}

function labelValue(
  ctx: CanvasRenderingContext2D,
  label: string | null,
  value: unknown,
  x: number,
  y: number,
  w: number,
  h: number,
  valueColor: string = T.text,
  rightPad = 10,
): void {
  ctx.textBaseline = 'middle'
  ctx.font = font(11.5)
  ctx.textAlign = 'left'
  ctx.fillStyle = T.text3
  const labelWidth = label ? ctx.measureText(label).width : 0
  if (label) ctx.fillText(label, x + 10, y + h / 2 + 0.5)
  ctx.textAlign = 'right'
  ctx.fillStyle = valueColor
  const maxW = w - 10 - labelWidth - 10 - rightPad
  ctx.fillText(fitText(ctx, value, Math.max(0, maxW)), x + w - rightPad, y + h / 2 + 0.5)
  ctx.textAlign = 'left'
  ctx.textBaseline = 'alphabetic'
}

function drawArrow(ctx: CanvasRenderingContext2D, x: number, y: number, dir: 1 | -1): void {
  ctx.fillStyle = T.text3
  ctx.beginPath()
  ctx.moveTo(x + dir * 3, y)
  ctx.lineTo(x - dir * 1.5, y - 3.5)
  ctx.lineTo(x - dir * 1.5, y + 3.5)
  ctx.closePath()
  ctx.fill()
}

function comboOptions(widget: IWidget): string[] | Record<string, string> {
  const values = widget.options?.values
  return typeof values === 'function' ? values(widget) : (values ?? [])
}

function comboText(widget: IWidget): string {
  const values = comboOptions(widget)
  if (Array.isArray(values)) return String(widget.value)
  return values[widget.value] ?? String(widget.value)
}

/** Installs Hazor-style widget drawing and routes combo clicks to the Hazor menu. */
export function installCanvasWidgets(): void {
  const proto = LGraphCanvas.prototype as unknown as {
    drawNodeWidgets: (
      node: LGraphNode,
      posY: number,
      ctx: CanvasRenderingContext2D,
      activeWidget: IWidget | null,
    ) => void
    processNodeWidgets: (node: LGraphNode, pos: Vector2, event: Event, activeWidget: IWidget | null) => IWidget | null
  }

  proto.drawNodeWidgets = function (this: LGraphCanvas, node, posY, ctx, activeWidget) {
    const widgets = nodeWidgets(node)
    if (!widgets?.length) return
    const width = node.size[0]
    const showText = this.ds.scale > 0.5
    const graphMouse = (this as unknown as UndeclaredCanvasMembers).graph_mouse
    const mouseX = graphMouse[0] - node.pos[0]
    const mouseY = graphMouse[1] - node.pos[1]
    let y = posY + 2
    ctx.save()
    ctx.globalAlpha = this.editor_alpha

    for (const widget of widgets) {
      const widgetY = widget.y ?? y
      widget.last_y = widgetY
      const widgetWidth = widget.width ?? width
      const h = widget.computeSize ? widget.computeSize(widgetWidth)[1] : H
      const hover =
        Boolean(node.mouseOver) &&
        mouseX > M &&
        mouseX < widgetWidth - M &&
        mouseY > widgetY &&
        mouseY < widgetY + h
      if (widget.disabled) ctx.globalAlpha = this.editor_alpha * 0.5

      switch (widget.type) {
        case 'combo': {
          const active = hover || activeWidget === widget
          box(ctx, M, widgetY, widgetWidth - M * 2, H, active, widget.disabled)
          if (showText) {
            labelValue(ctx, widget.name, comboText(widget), M, widgetY, widgetWidth - M * 2, H, T.text, 22)
            drawIcon(ctx, 'chevronDown', widgetWidth - M - 16, widgetY + H / 2 - 5, 10, T.text3)
          }
          break
        }
        case 'number': {
          const active = hover || activeWidget === widget
          box(ctx, M, widgetY, widgetWidth - M * 2, H, active, widget.disabled)
          if (showText) {
            const precision = widget.options?.precision ?? 3
            const text = Number(widget.value).toFixed(precision)
            labelValue(ctx, widget.name, text, M, widgetY, widgetWidth - M * 2, H)
            if (active && !widget.disabled) {
              drawArrow(ctx, M + 5, widgetY + H / 2, -1)
              drawArrow(ctx, widgetWidth - M - 5, widgetY + H / 2, 1)
            }
          }
          break
        }
        case 'toggle': {
          box(ctx, M, widgetY, widgetWidth - M * 2, H, hover, widget.disabled)
          if (showText) labelValue(ctx, widget.name, '', M, widgetY, widgetWidth - M * 2, H)
          const sx = widgetWidth - M - 10 - 26
          const sy = widgetY + (H - 14) / 2
          ctx.fillStyle = widget.value ? T.accent : T.toggleOff
          roundRect(ctx, sx, sy, 26, 14, 7)
          ctx.fill()
          ctx.fillStyle = widget.value ? '#fff' : T.toggleKnobOff
          ctx.beginPath()
          ctx.arc(widget.value ? sx + 19 : sx + 7, sy + 7, 5, 0, Math.PI * 2)
          ctx.fill()
          break
        }
        case 'text': {
          box(ctx, M, widgetY, widgetWidth - M * 2, H, hover, widget.disabled)
          if (showText) labelValue(ctx, widget.name, widget.value, M, widgetY, widgetWidth - M * 2, H)
          break
        }
        default:
          widget.draw?.(ctx, node, widgetWidth, widgetY, H, { hover, showText, mouse: [mouseX, mouseY] })
      }
      y = widgetY + h + T.rowGap
      ctx.globalAlpha = this.editor_alpha
    }
    ctx.restore()
    ctx.textAlign = 'left'
  }

  // Combo click -> the Hazor popup menu, instead of litegraph.ContextMenu.
  const nativeProcess = proto.processNodeWidgets
  proto.processNodeWidgets = function (this: LGraphCanvas, node, pos, event, activeWidget) {
    const pointerDown = `${(LiteGraph as unknown as UndeclaredLiteGraphConstants).pointerevents_method}down`
    const widgets = nodeWidgets(node)
    if (widgetHooks.openMenu && event.type === pointerDown && widgets) {
      const x = pos[0] - node.pos[0]
      const y = pos[1] - node.pos[1]
      for (const widget of widgets) {
        if (widget.type !== 'combo' || widget.disabled || widget.last_y === undefined) continue
        const widgetWidth = widget.width ?? node.size[0]
        if (x < 6 || x > widgetWidth - 12 || y < widget.last_y || y > widget.last_y + H) continue
        const values = comboOptions(widget)
        const isArray = Array.isArray(values)
        const keys = isArray ? values : Object.keys(values)
        const rect = graphRectToClient(this, node, M, widget.last_y, widgetWidth - M * 2, H)
        const canvas = this
        widgetHooks.openMenu({
          x: rect.left,
          y: rect.top + rect.height + 2,
          minWidth: Math.round(rect.width),
          items: keys.map((key) => ({
            label: isArray ? key : values[key],
            checked: isArray ? key === widget.value : key === String(widget.value),
            action: () => {
              const previous = widget.value
              widget.value = key
              if (widget.property && node.properties[widget.property] !== undefined) {
                node.setProperty(widget.property, widget.value)
              }
              widget.callback?.call(widget, widget.value, canvas, node, pos, event as MouseEvent)
              asNodeWithWidgetHook(node).onWidgetChanged?.(widget.name ?? '', widget.value, previous, widget)
              if (node.graph) (node.graph as unknown as GraphInternals)._version++
              canvas.setDirty(true, false)
            },
          })),
        })
        return widget
      }
    }
    return nativeProcess.call(this, node, pos, event, activeWidget)
  }
}

/* ===================================================================== */
/* Seed widget: a number plus a "randomise each run" toggle.              */
/* ===================================================================== */

export interface SeedWidget extends HazorWidget {
  value: number
  random: boolean
}

/**
 * `value` is the seed to show while not randomising; the backend's own
 * convention (-1 means "pick one") is what the property actually gets when
 * `random` is on, so no new contract field is needed for this.
 */
export function createSeedWidget(value: number, property: string): SeedWidget {
  const widget: SeedWidget = {
    name: 'Seed',
    value,
    random: value < 0,
    options: { property },
    draw(ctx, _node, width, y, _h, extra) {
      const hover = extra?.hover ?? false
      const showText = extra?.showText ?? true
      const bw = width - M * 2 - 24
      box(ctx, M, y, bw, H, hover && !widget.random)
      if (showText) {
        if (widget.random) labelValue(ctx, 'Seed', 'Random each run', M, y, bw, H, T.text3)
        else labelValue(ctx, 'Seed', String(widget.value), M, y, bw, H)
      }
      const ix = width - M - 20
      if (widget.random) {
        ctx.fillStyle = 'rgba(47,111,208,0.22)'
        roundRect(ctx, ix, y + 2, 20, H - 4, 3)
        ctx.fill()
      }
      drawIcon(ctx, 'dice', ix + 4, y + H / 2 - 6, 12, widget.random ? T.accentText : T.text3)
    },
    mouse(event, pos, node) {
      if (event.type !== 'pointerdown' && event.type !== 'mousedown') return false
      if (pos[0] > node.size[0] - M - 22) {
        widget.random = !widget.random
        node.setProperty(property, widget.random ? -1 : widget.value)
        node.setDirtyCanvas(true, false)
        return true
      }
      if (widget.random) return true
      const canvas = (LGraphCanvas as unknown as StaticActiveCanvas).active_canvas
      if (!canvas) return true
      canvas.prompt(
        'Seed',
        widget.value,
        (next: string) => {
          const parsed = Number.parseInt(next, 10)
          if (!Number.isNaN(parsed)) widget.value = Math.max(0, parsed)
          node.setProperty(property, widget.value)
          canvas.setDirty(true, false)
        },
        event,
      )
      return true
    },
  }
  return widget
}
