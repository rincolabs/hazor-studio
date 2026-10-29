<script setup lang="ts">
import { ref, watch } from 'vue'

import Tabs from '../../components/Tabs.vue'
import Header from '../../layout/Header.vue'
import PresetEditorShell from '../../presets/PresetEditorShell.vue'
import { usePresetEditor } from '../../services/usePresetEditor'
import AdvancedTab from './AdvancedTab.vue'
import BehaviorTab from './BehaviorTab.vue'
import ModelSettingsTab from './ModelSettingsTab.vue'
import ProviderTab from './ProviderTab.vue'

const props = defineProps<{ initialTab?: string }>()

const validTabs = ['provider', 'model-settings', 'behavior', 'advanced']
const tab = ref(props.initialTab && validTabs.includes(props.initialTab) ? props.initialTab : 'provider')

watch(
  () => props.initialTab,
  (next) => {
    if (next && validTabs.includes(next)) {
      tab.value = next
    }
  },
)

const editor = usePresetEditor('assistant')
</script>

<template>
  <main class="main">
    <Header title="Assistants" subtitle="Conversational agents used by Hazor Studio. The active preset powers the assistant panel." />
    <div class="body" style="flex-direction: row; flex-wrap: wrap; align-items: flex-start">
      <PresetEditorShell :editor="editor" list-label="Presets" kind-label="Assistant">
        <Tabs
          v-model="tab"
          ariaLabel="Preset settings"
          :items="[
            { id: 'provider', label: 'Provider' },
            { id: 'model-settings', label: 'Model Settings' },
            { id: 'behavior', label: 'Behavior' },
            { id: 'advanced', label: 'Advanced' },
          ]"
        />
        <ProviderTab v-if="tab === 'provider'" :editor="editor" />
        <ModelSettingsTab v-else-if="tab === 'model-settings'" :editor="editor" />
        <BehaviorTab v-else-if="tab === 'behavior'" :editor="editor" />
        <AdvancedTab v-else-if="tab === 'advanced'" :editor="editor" />
      </PresetEditorShell>
    </div>
  </main>
</template>
