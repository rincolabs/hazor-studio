# embedded-ui

The web interfaces Hazor Studio embeds through Qt WebEngine. One Vite project,
one entry point per panel.

This is not a web application. It has no server, no router and no deployment:
the build output is compiled into the Hazor binary as a Qt resource and loaded
from `qrc:/embedded-ui/<entry>/index.html`. The backend is C++, reached over
Qt WebChannel.

## Build

```bash
npm ci
npm run build
```

`npm run build` type-checks with `vue-tsc` first, so a frontend that does not
agree with the backend contract fails the build rather than the panel.

The output lands in `dist/`:

```
dist/
├── neural-flow/index.html
└── assets/*.js, *.css
```

Hazor's CMake build does this for you — see
[`docs/BUILD.md`](../docs/BUILD.md#embedded-web-interfaces-embedded-ui). It
runs `npm ci` only when `package-lock.json` changes, generates a `.qrc` from
`dist/` and compiles it with `rcc`. Node.js and npm are developer requirements;
the end user needs neither.

`npm run dev` serves the pages in a browser for layout work. Nothing beyond the
layout works there: with no Qt WebChannel, the page reports that it has no
backend instead of inventing one.

## Layout

```
src/
├── shared/                     Used by more than one panel
│   ├── bridge/qtBridge.ts      Qt WebChannel transport: connect, call, subscribe
│   └── design-system/          Tokens, base styling, and applying Hazor's theme
└── neural-flow/                The node editor
    ├── services/               The backend contract, its client, run status
    ├── nodes/                  Catalog -> LiteGraph node types and widgets
    ├── graph/                  LiteGraph setup, theming, workflow conversion
    └── components/             Vue components
```

## Rules

- **The backend owns the contract.** Node types, ports, property kinds,
  defaults and ranges all come from `sdlocal::describeNodeCatalog()` over the
  bridge. The editor keeps no copy: adding a node type in C++ makes it appear
  here with no change to this directory.
- **No second implementation.** Validation, model resolution, inference and
  layer insertion exist once each, in C++. The editor draws a graph, converts
  it to the executable schema and sends it.
- **Nothing remote.** No CDN, no font download, no telemetry. `base: './'` and
  a CSP that allows only `'self'` and `qrc:`.
- **Native prompt editing.** Catalog properties of kind `text` reserve a custom
  LiteGraph widget and place a DOM textarea over it. LiteGraph still owns node
  layout, pan and zoom; the browser owns the caret, selection, clipboard, line
  wrapping and text scrolling that a canvas widget cannot provide.
- **One project.** A second panel is a directory under `src/` and a line in
  `vite.config.ts`, not another `package.json`.

## Dependencies

| | |
|-|-|
| [Vue 3](https://vuejs.org) | interface |
| [Vite](https://vite.dev) | build |
| [litegraph.js](https://github.com/jagenjo/litegraph.js) `0.7.18` | node editor — the original project, `litegraph.core.js` bundle |

Versions are pinned exactly in `package.json` and locked in
`package-lock.json`, because this build output ships inside a binary.
