<script setup lang="ts">
import { computed, ref, watch } from 'vue'

import Tabs from '../../components/Tabs.vue'
import Header from '../../layout/Header.vue'
import { useModelsVision } from '../../services/useModelsVision'
import { useSdModels } from '../../services/useSdModels'
import StableDiffusionTab from './StableDiffusionTab.vue'
import VisionTab from './VisionTab.vue'

const props = defineProps<{ initialTab?: string }>()

const validTabs = ['vision', 'stable-diffusion']
const tab = ref(props.initialTab && validTabs.includes(props.initialTab) ? props.initialTab : 'vision')

watch(
  () => props.initialTab,
  (next) => {
    if (next && validTabs.includes(next)) {
      tab.value = next
    }
  },
)

const { state: visionState } = useModelsVision()
const { state: sdState } = useSdModels()

const invalidVisionCount = computed(
  () => visionState.value?.taskGroups.flatMap((g) => g.models).filter((m) => !m.isValid).length ?? 0,
)
const sdIssueCount = computed(
  () => (sdState.value?.entries.filter((e) => !e.available).length ?? 0) + (sdState.value?.directories.filter((d) => !d.available).length ?? 0),
)
</script>

<template>
  <main class="main">
    <Header title="Models" subtitle="Local AI models used by Hazor Studio tools and by Neural Flow." />
    <Tabs
      v-model="tab"
      ariaLabel="Model ecosystems"
      :items="[
        { id: 'vision', label: 'Vision & Editing', badge: invalidVisionCount > 0 ? { text: String(invalidVisionCount), kind: 'err' } : undefined },
        { id: 'stable-diffusion', label: 'Stable Diffusion', badge: sdIssueCount > 0 ? { text: String(sdIssueCount), kind: 'warn' } : undefined },
      ]"
    />
    <VisionTab v-if="tab === 'vision'" />
    <StableDiffusionTab v-else-if="tab === 'stable-diffusion'" />
  </main>
</template>
