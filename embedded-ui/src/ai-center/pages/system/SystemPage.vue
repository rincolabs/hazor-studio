<script setup lang="ts">
import { ref, watch } from 'vue'

import Tabs from '../../components/Tabs.vue'
import Header from '../../layout/Header.vue'
import DiagnosticsTab from './DiagnosticsTab.vue'
import PerformanceTab from './PerformanceTab.vue'
import RuntimeTab from './RuntimeTab.vue'
import ServicesTab from './ServicesTab.vue'

// `revision` changes on every navigation request, even one that repeats the
// current subsection — watching initialTab alone would ignore a second deep
// link to the tab the user has since navigated away from.
const props = defineProps<{ initialTab?: string; revision?: number }>()

const validTabs = ['runtime', 'performance', 'services', 'diagnostics']
const tab = ref(props.initialTab && validTabs.includes(props.initialTab) ? props.initialTab : 'runtime')

watch(
  () => props.revision,
  () => {
    const next = props.initialTab
    if (next && validTabs.includes(next)) {
      tab.value = next
    }
  },
)
</script>

<template>
  <main class="main">
    <Header title="System" subtitle="AI execution infrastructure and integrations. Global settings for all documents." />
    <Tabs
      v-model="tab"
      ariaLabel="System settings"
      :items="[
        { id: 'runtime', label: 'Runtime' },
        { id: 'performance', label: 'Performance' },
        { id: 'services', label: 'Services' },
        { id: 'diagnostics', label: 'Diagnostics' },
      ]"
    />
    <RuntimeTab v-if="tab === 'runtime'" />
    <PerformanceTab v-else-if="tab === 'performance'" />
    <ServicesTab v-else-if="tab === 'services'" />
    <DiagnosticsTab v-else-if="tab === 'diagnostics'" />
  </main>
</template>
