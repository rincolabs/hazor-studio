/**
 * Inline icon markup for the Neural Flow chrome (toolbar, menus, status).
 * Same visual language as the shared design system's icons
 * (`@shared` has no icon component of its own to reuse — see
 * `ai-center/components/icons.ts` for the sibling set that panel keeps).
 *
 * Category icons here must stay in sync with `graph/canvasTheme.ts`'s
 * `ICON_PATHS`, which draws the same shapes on the node canvas.
 */
export const ICONS = {
  flow: '<circle cx="5" cy="6" r="2.5"/><circle cx="19" cy="12" r="2.5"/><circle cx="5" cy="18" r="2.5"/><path d="M7.5 6h3a3 3 0 0 1 3 3v1.5M7.5 18h3a3 3 0 0 0 3-3v-1.5M13.5 12h3"/>',
  play: '<path d="M7 4.5v15l12-7.5z"/>',
  stop: '<rect x="6" y="6" width="12" height="12" rx="1.5"/>',
  refresh: '<path d="M20 11a8 8 0 0 0-14.9-3M4 4v4h4M4 13a8 8 0 0 0 14.9 3M20 20v-4h-4"/>',
  plus: '<path d="M12 5v14M5 12h14"/>',
  search: '<circle cx="11" cy="11" r="6"/><path d="m20 20-4.5-4.5"/>',
  chevronRight: '<path d="m9 6 6 6-6 6"/>',
  chevronDown: '<path d="m6 9 6 6 6-6"/>',
  close: '<path d="M6 6l12 12M18 6 6 18"/>',
  check: '<path d="m5 12 5 5L20 7"/>',
  warning: '<path d="M12 3 2 20h20z"/><path d="M12 10v4M12 17v.5"/>',
  cube: '<path d="M12 2 3 7v10l9 5 9-5V7z"/><path d="M3 7l9 5 9-5M12 12v10"/>',
  quote: '<path d="M4 6h16M4 11h16M4 16h10"/>',
  sliders: '<path d="M4 7h10M18 7h2M4 17h4M12 17h8"/><circle cx="16" cy="7" r="2"/><circle cx="10" cy="17" r="2"/>',
  import: '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M12 8v7M9 12l3 3 3-3"/>',
  sparkle: '<path d="M12 3l2 6 6 2-6 2-2 6-2-6-6-2 6-2z"/>',
  layers: '<path d="m12 3 9 5-9 5-9-5z"/><path d="m3 13 9 5 9-5"/>',
  loader: '<path d="M12 3a9 9 0 1 0 9 9"/>',
} as const

export type IconName = keyof typeof ICONS
