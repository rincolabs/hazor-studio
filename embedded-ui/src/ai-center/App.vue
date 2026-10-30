<script setup lang="ts">
import { computed } from 'vue'

import Banner from './components/Banner.vue'
import SideNav from './layout/SideNav.vue'
import AssistantsPage from './pages/assistants/AssistantsPage.vue'
import ImageGeneration from './pages/ImageGeneration.vue'
import ModelsPage from './pages/models/ModelsPage.vue'
import Overview from './pages/Overview.vue'
import SystemPage from './pages/system/SystemPage.vue'
import { useNavigation } from './services/useNavigation'
import { useOverview } from './services/useOverview'

const { section, subsection, revision, navigate } = useNavigation()
const { overview, error } = useOverview()

const alertCount = computed(() => overview.value?.alerts.length ?? 0)
const runtimeLabel = computed(() => {
  const diag = overview.value?.runtime.diagnostic
  if (!diag) {
    return 'Runtime: —'
  }
  if (diag.selectedProvider !== diag.activeProvider && diag.activeProvider !== '—') {
    return `Runtime: ${diag.activeProvider} (fallback)`
  }
  return `Runtime: ${diag.activeProvider}`
})
const runtimeOk = computed(() => {
  const diag = overview.value?.runtime.diagnostic
  return Boolean(diag && diag.status === 'ready' && diag.selectedProvider === diag.activeProvider)
})
const mcpLabel = computed(() => {
  const mcp = overview.value?.mcp
  if (!mcp) {
    return 'MCP server'
  }
  return mcp.enabled ? `MCP server · port ${mcp.port}` : 'MCP server · disabled'
})
const mcpOk = computed(() => Boolean(overview.value?.mcp.listening))
</script>

<template>
  <div class="app">
    <template v-if="error">
      <main class="main">
        <div class="body">
          <Banner severity="err" title="Could not connect to Hazor">{{ error }}</Banner>
        </div>
      </main>
    </template>
    <template v-else>
      <SideNav
        :current="section"
        :alert-count="alertCount"
        :runtime-label="runtimeLabel"
        :runtime-ok="runtimeOk"
        :mcp-label="mcpLabel"
        :mcp-ok="mcpOk"
        @navigate="navigate"
      />
      <Overview v-if="section === 'overview'" />
      <AssistantsPage v-else-if="section === 'assistants'" :initial-tab="subsection" :revision="revision" />
      <ImageGeneration v-else-if="section === 'image-generation'" />
      <ModelsPage v-else-if="section === 'models'" :initial-tab="subsection" :revision="revision" />
      <SystemPage v-else-if="section === 'system'" :initial-tab="subsection" :revision="revision" />
    </template>
  </div>
</template>
