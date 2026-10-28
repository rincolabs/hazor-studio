import { fileURLToPath, URL } from 'node:url'

import vue from '@vitejs/plugin-vue'
import { defineConfig } from 'vite'

/**
 * One Vite project for every interface Hazor embeds through Qt WebEngine.
 *
 * `src/` holds one directory per panel, each with its own `index.html`, and
 * `rollupOptions.input` lists them. Adding a panel later means adding a
 * directory and one line there — not a second Node project.
 *
 * The output is loaded out of the Qt resource system by the C++ side
 * (`qrc:/embedded-ui/<entry>/index.html`), so it must not assume a document
 * root, a dev server or a network: `base: './'` makes every asset reference
 * relative, and nothing here pulls from a CDN.
 */
export default defineConfig({
  root: fileURLToPath(new URL('./src', import.meta.url)),
  base: './',
  plugins: [vue()],
  resolve: {
    alias: [
      {
        find: /^@shared\//,
        replacement: fileURLToPath(new URL('./src/shared/', import.meta.url)),
      },
      {
        // The original litegraph.js ships two bundles, and which one matters:
        // `litegraph.js` is the editor *plus* its demo node library, whose
        // ~100 example types would all appear in the editor's "Add node" menu
        // next to Hazor's. `litegraph.core.js` is the editor alone.
        //
        // An exact-match pattern, so `litegraph.js/css/litegraph.css` is still
        // resolved by the package itself.
        find: /^litegraph\.js$/,
        replacement: fileURLToPath(
          new URL('./node_modules/litegraph.js/build/litegraph.core.js', import.meta.url),
        ),
      },
    ],
  },
  build: {
    outDir: fileURLToPath(new URL('./dist', import.meta.url)),
    emptyOutDir: true,
    // There is exactly one browser to support: the Chromium inside the Qt
    // WebEngine this project pins (6.8 → Chromium 122). Nothing older ever
    // loads these pages, so nothing is transpiled down for one.
    target: 'es2022',
    assetsDir: 'assets',
    // A preload polyfill exists to hide network latency. Reading out of the
    // binary has none.
    modulePreload: { polyfill: false },
    sourcemap: false,
    rollupOptions: {
      input: {
        'neural-flow': fileURLToPath(new URL('./src/neural-flow/index.html', import.meta.url)),
      },
    },
  },
})
