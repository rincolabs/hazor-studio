/**
 * Design constants for the node canvas.
 *
 * Canvas 2D cannot read CSS custom properties while drawing, so these mirror
 * the Neural Flow block of `@shared/design-system/tokens.css` — same names
 * (translated to camelCase), same values. Keep both in sync.
 *
 * Only the "B — Tinted" node style is implemented: it is the mandated visual
 * for this editor, and the prototype's "A — Panel" / "C — Compact"
 * alternatives are dropped rather than carried as dead branches.
 */

export const T = {
  font: 'Inter, "Segoe UI", system-ui, sans-serif',
  mono: '"Cascadia Mono", Consolas, ui-monospace, monospace',

  // workspace — mirrors --ws-bg (the grid colours below have no DOM
  // equivalent to mirror; the canvas is their only consumer)
  wsBg: '#232526',
  gridMinor: 'rgba(255,255,255,0.035)',
  gridMajor: 'rgba(255,255,255,0.07)',

  // node frame — mirrors --node-*
  nodeBg: '#3e4041',
  nodeHead: '#37393a',
  nodeBorder: '#2a2c2d',
  nodeBorderHover: '#6a6c6d',
  accent: '#2f6fd0',
  accentText: '#8db8f5',
  running: '#3f73c2',
  error: '#c4554b',
  errorBg: '#432c2d',
  errorBadgeBg: '#563334', // mirrors --err-bg; lighter than the footer's errorBg
  errorText: '#f5c9c3',
  errorIcon: '#ffa49a',
  infoBg: '#2c3f5c',
  info: '#a9c8f7',
  shadow: 'rgba(0,0,0,0.35)',

  // text
  text: '#ffffff',
  text2: '#d3d3d3',
  text3: '#b6b8b9',
  textDisabled: '#8c8e8f',

  // widgets — mirrors --widget-*
  widgetBg: '#303233',
  widgetBgHover: '#36383a',
  widgetBorder: '#4a4c4d',
  widgetBorderHover: '#5f6162',
  toggleOff: '#5b5d5e',
  toggleKnobOff: '#cfd1d2',

  // metrics (px, graph space) — mirrors --toolbar-h sibling tokens
  radius: 5,
  titleH: 34,
  rowH: 22,
  rowGap: 4,
  padX: 10,
  padTop: 6,
  groupH: 18,
  socketR: 5,
  widgetR: 3,
} as const

/** Connection (port) type -> colour. Mirrors --sock-* in the shared tokens. */
export const SOCKET_COLORS: Record<string, string> = {
  model: '#6ea2ff',
  clip: '#f3dc6a',
  vae: '#ff6e6e',
  engine: '#8c8f91',
  conditioning: '#7fd18b',
  references: '#a8d9b4',
  latent: '#d66bd6',
  hires: '#f2c98a',
  lora: '#b48cf2',
  image: '#e6e7e8',
  mask: '#9a9ea1',
}

/** Node category -> [icon colour, header tint]. Mirrors --cat-N and --cat-N-tint. */
export const CATEGORY_COLORS: Record<string, readonly [string, string]> = {
  models: ['#6ea2ff', '#2e3d55'],
  conditioning: ['#7fd18b', '#2d4433'],
  latent: ['#d66bd6', '#43304a'],
  sampling: ['#f0b45a', '#4a3d27'],
  input: ['#5fc9c4', '#274443'],
  processing: ['#e08fc0', '#46303f'],
  output: ['#d3d3d3', '#47494b'],
}

/** Node category -> icon name (see ICON_PATHS). */
export const CATEGORY_ICONS: Record<string, string> = {
  models: 'cube',
  conditioning: 'quote',
  latent: 'sparkle',
  sampling: 'sliders',
  input: 'import',
  processing: 'sparkle',
  output: 'layers',
}

/**
 * Stroke icons the canvas draws, as SVG path `d` strings on a 24x24 grid —
 * the same shapes `components/ui/icons.ts` draws in the DOM, kept as data
 * here because Canvas 2D takes a `Path2D`, not markup.
 */
export const ICON_PATHS: Record<string, readonly string[]> = {
  cube: ['M12 2 3 7v10l9 5 9-5V7z', 'M3 7l9 5 9-5M12 12v10'],
  quote: ['M4 6h16M4 11h16M4 16h10'],
  sliders: [
    'M4 7h10M18 7h2M4 17h4M12 17h8',
    'M18 7a2 2 0 1 1-4 0 2 2 0 0 1 4 0',
    'M12 17a2 2 0 1 1-4 0 2 2 0 0 1 4 0',
  ],
  import: [
    'M5 4h14a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2z',
    'M12 8v7M9 12l3 3 3-3',
  ],
  sparkle: ['M12 3l2 6 6 2-6 2-2 6-2-6-6-2 6-2z'],
  layers: ['m12 3 9 5-9 5-9-5z', 'm3 13 9 5 9-5'],
  chevronDown: ['m6 9 6 6 6-6'],
  dice: [
    'M7 4h10a3 3 0 0 1 3 3v10a3 3 0 0 1-3 3H7a3 3 0 0 1-3-3V7a3 3 0 0 1 3-3z',
    'M9 9h.01M15 15h.01M15 9h.01M9 15h.01',
  ],
  alert: ['M12 3 2 20h20z', 'M12 10v4M12 17v.5'],
}
