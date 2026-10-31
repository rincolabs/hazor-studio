/**
 * Glue between an `LGraphCanvas` and the Hazor Vue menu system, for the
 * original LiteGraph (jagenjo/litegraph.js 0.7.x).
 *
 *  - Shift+A           -> `openAddMenu(clientX, clientY, pickType)`
 *  - right-click empty -> `openCanvasMenu(clientX, clientY, pickType)`
 *    (right-click on a node, slot or group keeps litegraph's own menu,
 *    themed by `litegraph-theme.css`)
 *  - double-click empty canvas (litegraph's own search box) routes to the
 *    Add menu too
 *  - picking a node type creates it under the cursor in "grab" mode: it
 *    follows the mouse until the next click, Blender-style
 *
 * Canvas sizing and the textarea overlay are unrelated concerns, handled
 * where the canvas element itself is owned (`graph/litegraph.ts`,
 * `nodes/textareaWidget.ts`).
 */
import { LGraphCanvas, LiteGraph } from 'litegraph.js'
import type { LGraphNode } from 'litegraph.js'

import type { CorrectlyTypedShowSearchBox, UndeclaredCanvasMembers } from './litegraphInternals'

export interface CanvasMenuBridgeOptions {
  canvas: LGraphCanvas
  openAddMenu: (clientX: number, clientY: number, pickType: (type: string) => void) => void
  openCanvasMenu: (clientX: number, clientY: number, pickType: (type: string) => void) => void
}

export interface CanvasMenuBridge {
  pickType: (type: string) => void
  detach: () => void
}

function extras(canvas: LGraphCanvas): UndeclaredCanvasMembers {
  return canvas as unknown as UndeclaredCanvasMembers
}

/**
 * Installs the routing that is global to every canvas: a right-click on
 * empty space, and litegraph's own double-click search box, both go to the
 * Hazor menus instead of litegraph's native ones. Call once.
 */
export function installCanvasMenuRouting(): void {
  const searchProto = LGraphCanvas.prototype as unknown as CorrectlyTypedShowSearchBox
  const nativeSearch = searchProto.showSearchBox
  searchProto.showSearchBox = function (this: LGraphCanvas, event, options) {
    const onSearch = extras(this).hz_onSearch
    if (onSearch && event) {
      onSearch(event)
      return
    }
    nativeSearch.call(this, event, options)
  }

  const nativeContextMenu = LGraphCanvas.prototype.processContextMenu
  LGraphCanvas.prototype.processContextMenu = function (node, event) {
    const mouseEvent = event as MouseEvent & { canvasX: number; canvasY: number }
    const onGroup = !node && this.graph?.getGroupOnPos(mouseEvent.canvasX, mouseEvent.canvasY)
    const onCanvasMenu = extras(this).hz_onCanvasMenu
    if (!node && !onGroup && onCanvasMenu) {
      onCanvasMenu(mouseEvent)
      return
    }
    nativeContextMenu.call(this, node, event)
  }
}

/** Per-canvas: Shift+A and the two custom routing fields `installCanvasMenuRouting` reads. */
export function attachCanvasMenuBridge(options: CanvasMenuBridgeOptions): CanvasMenuBridge {
  const { canvas, openAddMenu, openCanvasMenu } = options
  const element = canvas.canvas
  const mouse = { x: 0, y: 0, inside: false }

  function pickType(type: string): void {
    const node: LGraphNode | null = LiteGraph.createNode(type)
    if (!node) return
    const graphMouse = extras(canvas).graph_mouse
    const gx = graphMouse[0]
    const gy = graphMouse[1]
    // cursor over the middle of the title, away from the collapse box in the
    // first title-height square.
    node.pos = [gx - node.size[0] / 2, gy + LiteGraph.NODE_TITLE_HEIGHT / 2]
    canvas.graph.add(node)
    canvas.deselectAllNodes()
    canvas.selectNode(node)
    // Grab mode: litegraph moves the selected node while `node_dragged` is
    // set; the next click drops it.
    canvas.node_dragged = node
    canvas.setDirty(true, true)
  }

  const onPointerMove = (event: PointerEvent): void => {
    mouse.x = event.clientX
    mouse.y = event.clientY
    mouse.inside = true
  }
  const onPointerLeave = (): void => {
    mouse.inside = false
  }
  element.addEventListener('pointermove', onPointerMove)
  element.addEventListener('pointerleave', onPointerLeave)

  function onKey(event: KeyboardEvent): void {
    const target = event.target as HTMLElement | null
    if (target && (/^(INPUT|TEXTAREA|SELECT)$/.test(target.tagName) || target.isContentEditable)) return
    if (!event.shiftKey || event.ctrlKey || event.metaKey || (event.key !== 'A' && event.key !== 'a')) return
    event.preventDefault()
    event.stopPropagation()
    const rect = element.getBoundingClientRect()
    const x = mouse.inside ? mouse.x : rect.left + rect.width / 2 - 110
    const y = mouse.inside ? mouse.y : rect.top + 60
    if (!mouse.inside) {
      // Keep graph_mouse consistent with where the menu opens.
      const graphMouse = extras(canvas).graph_mouse
      const ratio = rect.width > 0 ? element.width / rect.width : 1
      graphMouse[0] = ((x - rect.left) * ratio) / canvas.ds.scale - canvas.ds.offset[0]
      graphMouse[1] = ((y - rect.top) * ratio) / canvas.ds.scale - canvas.ds.offset[1]
    }
    openAddMenu(x, y, pickType)
  }
  window.addEventListener('keydown', onKey, true)

  extras(canvas).hz_onCanvasMenu = (event: MouseEvent) => openCanvasMenu(event.clientX, event.clientY, pickType)
  extras(canvas).hz_onSearch = (event: MouseEvent) => openAddMenu(event.clientX, event.clientY, pickType)

  return {
    pickType,
    detach() {
      element.removeEventListener('pointermove', onPointerMove)
      element.removeEventListener('pointerleave', onPointerLeave)
      window.removeEventListener('keydown', onKey, true)
      extras(canvas).hz_onCanvasMenu = undefined
      extras(canvas).hz_onSearch = undefined
    },
  }
}
