/**
 * Single global popup-menu host (one menu open at a time, like Blender).
 * Backs both the Add Node menu and a combo widget's dropdown.
 */
import { reactive } from 'vue'

export interface MenuItem {
  label: string
  icon?: string
  iconColor?: string
  hint?: string
  swatch?: string
  swatchTitle?: string
  shortcut?: string
  checked?: boolean
  disabled?: boolean
  separator?: boolean
  empty?: boolean
  children?: MenuItem[]
  action?: () => void
}

export interface OpenMenuOptions {
  /** Viewport position of the menu's top-left corner (clamped to the window). */
  x: number
  y: number
  title?: string
  items: MenuItem[]
  /** Shows a search field at the top; typing filters `searchItems`. */
  search?: boolean
  /** Flat list of leaf items used for filtering when `search` is set. */
  searchItems?: MenuItem[]
  /** e.g. the width of the widget that opened a dropdown. */
  minWidth?: number
}

interface MenuState extends Required<Omit<OpenMenuOptions, 'title'>> {
  open: boolean
  id: number
  title: string
}

export const menuState: MenuState = reactive({
  open: false,
  id: 0,
  x: 0,
  y: 0,
  title: '',
  items: [],
  search: false,
  searchItems: [],
  minWidth: 0,
})

export function openMenu(options: OpenMenuOptions): void {
  Object.assign(menuState, {
    title: '',
    search: false,
    searchItems: [],
    minWidth: 0,
    ...options,
    open: true,
    id: menuState.id + 1,
  })
}

export function closeMenu(): void {
  menuState.open = false
}
