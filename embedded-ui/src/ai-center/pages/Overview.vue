<script setup lang="ts">
import { computed, ref } from 'vue'

import Banner from '../components/Banner.vue'
import Icon from '../components/Icon.vue'
import Header from '../layout/Header.vue'
import { useNavigation } from '../services/useNavigation'
import { useOverview } from '../services/useOverview'

const { navigate } = useNavigation()
const { overview, error } = useOverview()

const updatedAt = computed(() =>
  overview.value
    ? new Date().toLocaleTimeString(undefined, { hour: '2-digit', minute: '2-digit' })
    : '',
)

const providerLabel = ref<Record<string, string>>({
  local: 'Local',
  openai: 'OpenAI',
  anthropic: 'Anthropic',
  google: 'Google',
  custom: 'Custom',
})
</script>

<template>
  <main class="main">
    <Header title="Overview" subtitle="Operational summary of the AI environment. Select a card to open its configuration.">
      <span v-if="updatedAt" class="muted" style="font-size: 11px">Status updated {{ updatedAt }}</span>
    </Header>

    <div class="body">
      <Banner v-if="error" severity="err" title="Could not load the AI environment">{{ error }}</Banner>

      <template v-if="overview">
        <section v-if="overview.alerts.length > 0" aria-labelledby="att" style="display: flex; flex-direction: column; gap: 8px">
          <h2 id="att" style="margin: 0; font-size: 12.5px; font-weight: 600; color: var(--text)">
            Attention needed · {{ overview.alerts.length }}
          </h2>
          <Banner v-for="(alert, index) in overview.alerts" :key="index" :severity="alert.severity" :title="alert.title">
            {{ alert.detail }}
            <template #action>
              <a class="act" href="#" @click.prevent="navigate(alert.section, alert.subsection)">{{ alert.linkLabel }}</a>
            </template>
          </Banner>
        </section>

        <div style="display: grid; grid-template-columns: repeat(auto-fill, minmax(300px, 1fr)); gap: 12px">
          <!-- AI Runtime -->
          <article class="card">
            <div class="card-h">
              <Icon name="system" size="lg" />AI Runtime
              <span class="bdg" :class="overview.runtime.settings.enabled ? 'ok' : ''">
                <span class="dot" />{{ overview.runtime.settings.enabled ? 'Enabled' : 'Disabled' }}
              </span>
            </div>
            <div class="kv">
              <span class="k">Availability</span>
              <span class="v">
                {{ overview.runtime.diagnostic.runtimeInstalled ? `Available · ONNX Runtime ${overview.runtime.onnxRuntimeVersion}` : 'Not available in this build' }}
              </span>
              <span class="k">Selected provider</span><span class="v">{{ overview.runtime.diagnostic.selectedProvider }}</span>
              <span class="k">Active provider</span>
              <span class="v" style="display: flex; gap: 6px; align-items: center">
                {{ overview.runtime.diagnostic.activeProvider }}
                <span v-if="overview.runtime.diagnostic.selectedProvider !== overview.runtime.diagnostic.activeProvider" class="bdg warn">Fallback</span>
              </span>
              <span class="k">CPU fallback</span><span class="v">{{ overview.runtime.settings.allowCpuFallback ? 'Allowed' : 'Not allowed' }}</span>
            </div>
            <div class="card-f"><a href="#" @click.prevent="navigate('system', 'runtime')">System › Runtime →</a></div>
          </article>

          <!-- Local Models -->
          <article class="card">
            <div class="card-h">
              <Icon name="models" size="lg" />Local Models
              <span class="bdg" :class="overview.models.invalid > 0 ? 'err' : 'ok'">
                <span class="dot" />{{ overview.models.invalid > 0 ? `${overview.models.invalid} invalid` : 'All valid' }}
              </span>
            </div>
            <div class="kv">
              <span class="k">Registered</span><span class="v">{{ overview.models.registered }} models · {{ overview.models.tasks }} tasks</span>
              <span class="k">Valid</span><span class="v">{{ overview.models.valid }}</span>
              <span class="k">Invalid</span>
              <span class="v" :class="overview.models.invalid > 0 ? 'err-t' : undefined">
                {{ overview.models.invalid }}<template v-if="overview.models.invalidNames.length"> · {{ overview.models.invalidNames.join(', ') }}</template>
              </span>
            </div>
            <div class="card-f"><a href="#" @click.prevent="navigate('models', 'vision')">Models →</a></div>
          </article>

          <!-- Assistant -->
          <article class="card">
            <div class="card-h">
              <Icon name="assistants" size="lg" />Assistant
              <span class="bdg" :class="overview.activeAssistant ? 'ok' : ''"><span class="dot" />{{ overview.activeAssistant ? 'Active' : 'None' }}</span>
            </div>
            <div class="kv">
              <span class="k">Active preset</span><span class="v">{{ overview.activeAssistant?.name ?? 'None configured' }}</span>
              <template v-if="overview.activeAssistant">
                <span class="k">Provider</span><span class="v">{{ providerLabel[overview.activeAssistant.providerType] }}</span>
                <span class="k">Model</span><span class="v mono">{{ overview.activeAssistant.model }}</span>
              </template>
            </div>
            <div class="card-f"><a href="#" @click.prevent="navigate('assistants')">Assistants →</a></div>
          </article>

          <!-- Image Generation -->
          <article class="card">
            <div class="card-h">
              <Icon name="imageGeneration" size="lg" />Image Generation
              <span class="bdg" :class="overview.activeGenerative ? 'ok' : ''"><span class="dot" />{{ overview.activeGenerative ? 'Active' : 'None' }}</span>
            </div>
            <div class="kv">
              <span class="k">Active preset</span><span class="v">{{ overview.activeGenerative?.name ?? 'None configured' }}</span>
              <template v-if="overview.activeGenerative">
                <span class="k">Provider</span><span class="v">{{ providerLabel[overview.activeGenerative.providerType] }}</span>
                <span class="k">Model</span><span class="v mono">{{ overview.activeGenerative.model }}</span>
              </template>
            </div>
            <div class="card-f"><a href="#" @click.prevent="navigate('image-generation')">Image Generation →</a></div>
          </article>

          <!-- Neural Flow / Stable Diffusion -->
          <article class="card">
            <div class="card-h">
              <Icon name="sdNode" size="lg" />Neural Flow / Stable Diffusion
              <span class="bdg" :class="overview.sd.missingPaths.length > 0 ? 'warn' : 'ok'">
                <span class="dot" />{{ overview.sd.missingPaths.length > 0 ? `${overview.sd.missingPaths.length} missing` : 'All available' }}
              </span>
            </div>
            <div class="kv">
              <span class="k">Registered models</span><span class="v">{{ overview.sd.registered }} · {{ overview.sd.categories }} categories</span>
              <span class="k">Directories</span>
              <span class="v">{{ overview.sd.directories }}<template v-if="overview.sd.unavailableDirectories > 0"> ({{ overview.sd.unavailableDirectories }} unavailable)</template></span>
              <span class="k">Missing paths</span>
              <span class="v" :class="overview.sd.missingPaths.length > 0 ? 'warn-t' : undefined">{{ overview.sd.missingPaths.length > 0 ? overview.sd.missingPaths.join(' · ') : 'None' }}</span>
              <span class="k">Default checkpoint</span><span class="v mono">{{ overview.sd.defaultModelId || '—' }}</span>
            </div>
            <div class="card-f"><a href="#" @click.prevent="navigate('models', 'stable-diffusion')">Models › Stable Diffusion →</a></div>
          </article>

          <!-- MCP Server -->
          <article class="card">
            <div class="card-h">
              <Icon name="mcp" size="lg" />MCP Server
              <span class="bdg" :class="overview.mcp.enabled ? 'ok' : ''"><span class="dot" />{{ overview.mcp.enabled ? 'Enabled' : 'Disabled' }}</span>
            </div>
            <div class="kv">
              <span class="k">Status</span><span class="v">{{ overview.mcp.listening ? 'Listening' : 'Not listening' }}</span>
              <span class="k">Port</span><span class="v mono">{{ overview.mcp.port }}</span>
              <span class="k">Used by</span><span class="v">Hazor assistant · external agents</span>
            </div>
            <div class="card-f"><a href="#" @click.prevent="navigate('system', 'services')">System › Services →</a></div>
          </article>
        </div>
      </template>
    </div>
  </main>
</template>
