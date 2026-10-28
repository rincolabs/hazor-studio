/**
 * Writes Hazor's active theme onto the CSS custom properties in `tokens.css`.
 *
 * The map arrives over the Qt WebChannel exactly as
 * `src/theme/WebThemeVariables.cpp` builds it, with camelCase keys; the tokens
 * are kebab-case and prefixed, so `textSecondary` becomes
 * `--hz-text-secondary`. Numbers are pixel lengths, which is what the two
 * numeric values in that map (font size and corner radius) are.
 *
 * Nothing is interpreted and no colour is computed: if a value needs changing,
 * it changes in the Theme that produced it.
 */
export type ThemeValues = Record<string, string | number>

const TOKEN_PREFIX = '--hz-'

function tokenName(key: string): string {
  return TOKEN_PREFIX + key.replace(/[A-Z]/g, (letter) => `-${letter.toLowerCase()}`)
}

export function applyThemeVariables(theme: ThemeValues | undefined): void {
  if (!theme) {
    return
  }
  const root = document.documentElement
  for (const [key, value] of Object.entries(theme)) {
    root.style.setProperty(
      tokenName(key),
      typeof value === 'number' ? `${value}px` : String(value),
    )
  }
}
