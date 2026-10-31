/** Builders for the Add Node menu and the workspace right-click menu, from the real backend catalog. */
import { CATEGORY_ICONS } from '../../graph/canvasTheme'
import type { MenuItem } from '../../composables/useMenu'
import { openMenu } from '../../composables/useMenu'
import type { NodeCatalog, NodeDefinition } from '../../services/contract'

const catColor = (id: string): string => `var(--cat-${id})`
const sockColor = (type: string): string => `var(--sock-${type})`

function nodeItem(node: NodeDefinition, catalog: NodeCatalog, onAdd: (node: NodeDefinition) => void, withHint = false): MenuItem {
  const out = node.outputs[0]
  return {
    label: node.label,
    hint: withHint ? catalog.categories.find((c) => c.id === node.category)?.label : undefined,
    swatch: out ? sockColor(out.type) : undefined,
    swatchTitle: out ? `Output: ${out.id}` : undefined,
    action: () => onAdd(node),
  }
}

export function categoryItems(catalog: NodeCatalog, onAdd: (node: NodeDefinition) => void): MenuItem[] {
  return catalog.categories
    .map((category) => ({
      label: category.label,
      icon: CATEGORY_ICONS[category.id],
      iconColor: catColor(category.id),
      children: catalog.nodes.filter((n) => n.category === category.id).map((n) => nodeItem(n, catalog, onAdd)),
    }))
    .filter((group) => group.children.length > 0)
}

/** Shift+A — "Add" menu with search on top and categories as submenus. */
export function openAddNodeMenu(
  catalog: NodeCatalog,
  x: number,
  y: number,
  onAdd: (node: NodeDefinition) => void,
): void {
  openMenu({
    x,
    y,
    title: 'Add',
    search: true,
    items: categoryItems(catalog, onAdd),
    searchItems: catalog.nodes.map((n) => nodeItem(n, catalog, onAdd, true)),
  })
}

/** Right-click on empty workspace. */
export function openWorkspaceContextMenu(
  catalog: NodeCatalog,
  x: number,
  y: number,
  onAdd: (node: NodeDefinition) => void,
): void {
  openMenu({
    x,
    y,
    items: [
      { label: 'Add Node', icon: 'plus', children: categoryItems(catalog, onAdd) },
      {
        label: 'Search Nodes…',
        icon: 'search',
        shortcut: 'Shift A',
        action: () => openAddNodeMenu(catalog, x, y, onAdd),
      },
    ],
  })
}
