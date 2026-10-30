<script setup lang="ts">
import { computed, ref } from 'vue'

import Dialog from '../../components/Dialog.vue'
import Icon from '../../components/Icon.vue'
import { useRuntime } from '../../services/useRuntime'

const { state, clearCache } = useRuntime()

const collectedAt = ref(new Date().toLocaleTimeString(undefined, { hour: '2-digit', minute: '2-digit' }))
const confirmingClear = ref(false)

const fallbackActive = computed(() => {
  const diag = state.value?.diagnostic
  return Boolean(diag && diag.selectedProvider !== diag.activeProvider)
})

function humanSize(bytes: number): string {
  if (bytes <= 0) return '0 MB'
  return `${(bytes / (1024 * 1024)).toFixed(0)} MB`
}

async function onClearCache(): Promise<void> {
  confirmingClear.value = false
  await clearCache()
  collectedAt.value = new Date().toLocaleTimeString(undefined, { hour: '2-digit', minute: '2-digit' })
}
</script>

<template>
  <div class="body">
    <div style="display: flex; flex-wrap: wrap; align-items: center; gap: 8px">
      <span class="muted">Read-only status · collected {{ collectedAt }}</span>
    </div>

    <div v-if="state" style="display: grid; grid-template-columns: repeat(auto-fit, minmax(380px, 1fr)); gap: 14px; align-items: start">
      <section class="card" aria-label="Environment">
        <div class="card-h">
          Environment
          <span class="bdg" :class="state.diagnostic.runtimeInstalled ? 'ok' : 'err'">
            <span class="dot" />{{ state.diagnostic.runtimeInstalled ? 'Compatible' : 'Not available' }}
          </span>
        </div>
        <div class="kv">
          <span class="k">Operating system</span><span class="v">{{ state.osName }}</span>
          <span class="k">CPU architecture</span><span class="v">{{ state.cpuArchitecture }}</span>
          <span class="k">ONNX Runtime</span><span class="v">{{ state.onnxRuntimeVersion || '—' }}</span>
          <span class="k">Runtime library</span><span class="v mono">{{ state.diagnostic.loadedFrom || '—' }}</span>
          <span class="k">Platform status</span>
          <span class="v" :class="state.diagnostic.runtimeInstalled ? 'ok-t' : 'err-t'">
            {{ state.diagnostic.runtimeInstalled ? 'Supported platform' : 'Not available in this build' }}
          </span>
        </div>
      </section>

      <section class="card" aria-label="Execution Providers">
        <div class="card-h">
          Execution Providers
          <span class="bdg" :class="fallbackActive ? 'warn' : 'ok'"><span class="dot" />{{ fallbackActive ? 'Fallback active' : 'As selected' }}</span>
        </div>
        <div class="kv">
          <span class="k">Available</span>
          <span class="v" style="display: flex; flex-wrap: wrap; gap: 4px">
            <span v-for="info in state.availableProviders" :key="info.id" class="bdg sq">{{ info.displayName }}</span>
          </span>
          <span class="k">Selected</span><span class="v">{{ state.diagnostic.selectedProvider }}</span>
          <span class="k">Active</span><span class="v">{{ state.diagnostic.activeProvider }}</span>
          <span class="k">Last fallback</span><span class="v">{{ state.diagnostic.lastFallback || 'None' }}</span>
        </div>
      </section>

      <section class="card" aria-label="Model and Cache Status" style="grid-column: 1 / -1">
        <div class="card-h">Model and Cache Status</div>
        <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(130px, 1fr)); gap: 8px">
          <div class="stat"><div class="stat-n">{{ state.bundledModelCount }}</div><div class="stat-l">Bundled models</div></div>
          <div class="stat"><div class="stat-n">{{ state.downloadedModelCount }}</div><div class="stat-l">Downloaded models</div></div>
          <div class="stat"><div class="stat-n">{{ state.customModelCount }}</div><div class="stat-l">Custom models</div></div>
          <div class="stat"><div class="stat-n" :class="state.invalidModelCount > 0 ? 'err-t' : undefined">{{ state.invalidModelCount }}</div><div class="stat-l">Invalid models</div></div>
          <div class="stat"><div class="stat-n">{{ state.cachedEmbeddingCount }}</div><div class="stat-l">Cached embeddings</div></div>
          <div class="stat"><div class="stat-n">{{ humanSize(state.cachedEmbeddingBytes) }}</div><div class="stat-l">Cache memory</div></div>
        </div>
        <div class="kv">
          <span class="k">Loaded now</span>
          <span class="v">{{ state.diagnostic.loadedSessions }} of {{ state.settings.maxLoadedModels }} max</span>
        </div>
      </section>

      <section class="card" aria-label="Troubleshooting" style="grid-column: 1 / -1">
        <div class="card-h">Troubleshooting</div>
        <div v-if="state.diagnostic.lastError">
          <span class="flbl">Last runtime error</span>
          <div class="out mono" style="color: var(--err)">{{ state.diagnostic.lastError }}</div>
        </div>
        <div v-else class="muted">No runtime errors recorded this session.</div>
        <div style="display: flex; flex-wrap: wrap; gap: 8px; align-items: center">
          <button type="button" class="btn danger" @click="confirmingClear = true">
            <Icon name="trash" />Clear AI Cache…
          </button>
          <span class="muted">Unloads model sessions and clears embedding and disk caches.</span>
        </div>
      </section>
    </div>

    <Dialog v-if="confirmingClear" title="Clear AI cache?" alert @close="confirmingClear = false">
      <p style="margin: 0">
        This unloads {{ state?.diagnostic.loadedSessions ?? 0 }} model session(s), removes
        {{ state?.cachedEmbeddingCount ?? 0 }} cached document embedding(s) ({{ humanSize(state?.cachedEmbeddingBytes ?? 0) }})
        and clears the on-disk cache. Models load again on next use.
      </p>
      <template #footer>
        <button type="button" class="btn ghost" @click="confirmingClear = false">Cancel</button>
        <button type="button" class="btn danger-fill" @click="onClearCache">Clear Cache</button>
      </template>
    </Dialog>
  </div>
</template>
