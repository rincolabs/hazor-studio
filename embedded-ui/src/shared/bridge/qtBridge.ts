/**
 * Qt WebChannel transport for Hazor's embedded interfaces.
 *
 * Transport only, on purpose: connecting the channel, looking an exposed
 * object up, calling a method and subscribing to a signal. What those methods
 * and signals mean belongs to the panel that owns them — the Neural Flow
 * contract lives in `neural-flow/services/neuralFlow.ts`, not here.
 *
 * `qwebchannel.js` itself comes from Qt, out of the same resource system that
 * serves the page (`qrc:///qtwebchannel/qwebchannel.js`, loaded by index.html).
 * Bundling a copy from npm would risk pairing one version of the JavaScript
 * half with a different version of the C++ half.
 */

/** A `signals:` member of an exposed QObject, as qwebchannel.js wraps it. */
export interface QtSignal<Args extends unknown[] = unknown[]> {
  connect(handler: (...args: Args) => void): void
  disconnect(handler: (...args: Args) => void): void
}

/** An object registered with `QWebChannel::registerObject`. */
export type QtRemoteObject = Record<string, unknown>

interface QtChannel {
  objects: Record<string, QtRemoteObject>
}

type QtChannelConstructor = new (
  transport: unknown,
  onReady: (channel: QtChannel) => void,
) => QtChannel

declare global {
  interface Window {
    QWebChannel?: QtChannelConstructor
    qt?: { webChannelTransport?: unknown }
  }
}

/**
 * Any failure to reach the backend. One error type, because every one of them
 * means the same thing to a panel: the page is on its own and has to say so
 * rather than silently showing an empty editor.
 */
export class QtBridgeError extends Error {
  constructor(message: string) {
    super(message)
    this.name = 'QtBridgeError'
  }
}

/**
 * How long to wait for a reply. Every method exposed to these pages returns
 * immediately — even starting a generation only validates and queues it — so a
 * silence this long is a lost message, not a slow one. Without a limit a lost
 * reply would leave the interface waiting forever with nothing on screen.
 */
const REPLY_TIMEOUT_MS = 30_000

/** True when the page is really inside Qt WebEngine with a channel to talk to. */
export function isHostedByQt(): boolean {
  return Boolean(window.qt?.webChannelTransport) && typeof window.QWebChannel === 'function'
}

let channel: Promise<QtChannel> | null = null

/** Connects the one channel this page gets, and caches it. */
export function connectChannel(): Promise<QtChannel> {
  if (channel) {
    return channel
  }
  channel = new Promise<QtChannel>((resolve, reject) => {
    const transport = window.qt?.webChannelTransport
    if (!transport) {
      reject(
        new QtBridgeError(
          'No Qt WebChannel transport. This page only runs inside Hazor, not in a browser.',
        ),
      )
      return
    }
    const Channel = window.QWebChannel
    if (typeof Channel !== 'function') {
      reject(
        new QtBridgeError(
          'qwebchannel.js did not load. Expected it at qrc:///qtwebchannel/qwebchannel.js.',
        ),
      )
      return
    }
    try {
      // The constructor calls back once the object list has arrived; there is
      // no error path on the Qt side, which is what the timeout below covers.
      const timer = window.setTimeout(() => {
        reject(new QtBridgeError('The Qt WebChannel did not finish connecting.'))
      }, REPLY_TIMEOUT_MS)
      new Channel(transport, (ready) => {
        window.clearTimeout(timer)
        resolve(ready)
      })
    } catch (error) {
      reject(new QtBridgeError(`The Qt WebChannel could not be created: ${describe(error)}`))
    }
  })
  // A failed connection must not be cached as a permanent verdict: the panel
  // may retry, and the next attempt deserves a fresh channel.
  channel.catch(() => {
    channel = null
  })
  return channel
}

/** The object `name` was registered under, or a rejection naming what is there. */
export async function remoteObject<T extends QtRemoteObject>(name: string): Promise<T> {
  const ready = await connectChannel()
  const object = ready.objects[name]
  if (!object) {
    const available = Object.keys(ready.objects).join(', ') || 'none'
    throw new QtBridgeError(
      `The backend did not expose "${name}". Registered objects: ${available}.`,
    )
  }
  return object as T
}

/**
 * Calls a method and waits for its result.
 *
 * The reply is taken through the trailing-callback form rather than the
 * Promise qwebchannel.js also returns: the callback is the one convention that
 * has been there since Qt 5, and this way the timeout is ours.
 */
export function invoke<Result>(
  target: QtRemoteObject,
  method: string,
  args: unknown[] = [],
  timeoutMs: number = REPLY_TIMEOUT_MS,
): Promise<Result> {
  const callable = target[method]
  if (typeof callable !== 'function') {
    return Promise.reject(
      new QtBridgeError(`The backend object has no invokable "${method}".`),
    )
  }
  return new Promise<Result>((resolve, reject) => {
    let settled = false
    const timer = window.setTimeout(() => {
      if (settled) {
        return
      }
      settled = true
      reject(new QtBridgeError(`"${method}" did not answer within ${timeoutMs} ms.`))
    }, timeoutMs)

    try {
      ;(callable as (...callArgs: unknown[]) => void).call(target, ...args, (result: Result) => {
        if (settled) {
          return
        }
        settled = true
        window.clearTimeout(timer)
        resolve(result)
      })
    } catch (error) {
      settled = true
      window.clearTimeout(timer)
      reject(new QtBridgeError(`"${method}" could not be called: ${describe(error)}`))
    }
  })
}

/**
 * Calls a method whose result nobody waits for.
 *
 * Separate from `invoke` so a void operation — cancelling, logging — does not
 * depend on whether a reply comes back for it at all.
 */
export function send(target: QtRemoteObject, method: string, args: unknown[] = []): void {
  const callable = target[method]
  if (typeof callable !== 'function') {
    throw new QtBridgeError(`The backend object has no invokable "${method}".`)
  }
  ;(callable as (...callArgs: unknown[]) => void).call(target, ...args)
}

/** Subscribes to a signal and returns the unsubscribe. */
export function onSignal<Args extends unknown[]>(
  target: QtRemoteObject,
  name: string,
  handler: (...args: Args) => void,
): () => void {
  const signal = target[name] as QtSignal<Args> | undefined
  if (!signal || typeof signal.connect !== 'function') {
    throw new QtBridgeError(`The backend object has no signal "${name}".`)
  }
  signal.connect(handler)
  return () => {
    signal.disconnect(handler)
  }
}

function describe(error: unknown): string {
  return error instanceof Error ? error.message : String(error)
}
