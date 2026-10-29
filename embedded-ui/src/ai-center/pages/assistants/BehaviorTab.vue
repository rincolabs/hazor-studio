<script setup lang="ts">
import { computed, ref } from 'vue'

import Group from '../../components/Group.vue'
import Icon from '../../components/Icon.vue'
import SegmentedControl from '../../components/SegmentedControl.vue'
import type { ToolCatalogGroup } from '../../services/contract'
import { getClient } from '../../services/client'
import type { PresetEditor } from '../../services/usePresetEditor'
import TestPromptGroup from './TestPromptGroup.vue'

const props = defineProps<{ editor: PresetEditor }>()

const catalog = ref<ToolCatalogGroup[]>([])
const filter = ref('')

void getClient().then(async (client) => {
  catalog.value = await client.toolCatalog()
})

const allToolIds = computed(() => catalog.value.flatMap((g) => g.tools))

const enabledCount = computed(() => {
  const enabled = new Set(props.editor.draft.value?.behavior.enabledTools ?? [])
  return allToolIds.value.filter((t) => enabled.has(t)).length
})

const filteredCatalog = computed(() => {
  const needle = filter.value.trim().toLowerCase()
  if (!needle) return catalog.value
  return catalog.value
    .map((group) => ({ ...group, tools: group.tools.filter((t) => t.toLowerCase().includes(needle)) }))
    .filter((group) => group.tools.length > 0)
})

function isEnabled(tool: string): boolean {
  return props.editor.draft.value?.behavior.enabledTools.includes(tool) ?? false
}

function setEnabled(tool: string, enabled: boolean): void {
  const preset = props.editor.draft.value
  if (!preset) return
  const set = new Set(preset.behavior.enabledTools)
  if (enabled) set.add(tool)
  else set.delete(tool)
  preset.behavior.enabledTools = [...set]
}

function groupEnabledCount(group: ToolCatalogGroup): number {
  return group.tools.filter((t) => isEnabled(t)).length
}

function toggleGroup(group: ToolCatalogGroup): void {
  const allOn = groupEnabledCount(group) === group.tools.length
  for (const tool of group.tools) {
    setEnabled(tool, !allOn)
  }
}

function selectAll(): void {
  if (props.editor.draft.value) {
    props.editor.draft.value.behavior.enabledTools = [...allToolIds.value]
  }
}

function clearAll(): void {
  if (props.editor.draft.value) {
    props.editor.draft.value.behavior.enabledTools = []
  }
}
</script>

<template>
  <template v-if="editor.draft.value">
    <Group title="System Prompt">
      <template #meta>{{ editor.draft.value.behavior.systemPrompt.length }} characters</template>
      <textarea v-model="editor.draft.value.behavior.systemPrompt" class="in" rows="7" aria-label="System prompt" style="font-size: 12px" />
      <div class="help" style="margin-top: 0">Sent at the start of every conversation with this preset.</div>
    </Group>

    <Group title="Tools">
      <div class="row">
        <span class="lbl">Tool Mode</span>
        <div>
          <SegmentedControl
            v-model="editor.draft.value.behavior.toolMode"
            aria-label="Tool mode"
            :options="[
              { value: 'auto', label: 'Auto' },
              { value: 'required', label: 'Required' },
              { value: 'none', label: 'None' },
            ]"
          />
          <div class="help">Auto: the model decides when to call tools. Required: every reply must call a tool. None: tools are never offered.</div>
        </div>
      </div>
      <div class="sep" />
      <div style="display: flex; flex-wrap: wrap; align-items: center; gap: 8px">
        <span class="lbl" style="padding: 0">Enabled Tools</span>
        <span class="bdg info">{{ enabledCount }} of {{ allToolIds.length }} enabled</span>
        <span style="flex: 1" />
        <div style="position: relative; flex: 0 1 240px">
          <input v-model="filter" class="in" placeholder="Filter tools…" aria-label="Filter tools" style="padding-left: 26px" />
          <Icon name="search" class="muted" style="position: absolute; left: 8px; top: 6px" />
        </div>
        <button type="button" class="btn ghost" @click="selectAll">Select All</button>
        <button type="button" class="btn ghost" @click="clearAll">Clear</button>
      </div>

      <div style="border: 1px solid var(--line-soft); border-radius: 3px">
        <template v-for="group in filteredCatalog" :key="group.id">
          <div style="display: flex; align-items: center; gap: 8px; padding: 7px 10px; background: var(--bg-3); border-top: 1px solid var(--line-soft)">
            <Icon name="chevronDown" />
            <input
              type="checkbox"
              class="chk"
              :checked="groupEnabledCount(group) === group.tools.length"
              :aria-label="`All ${group.label} tools`"
              @change="toggleGroup(group)"
            />
            <span class="strong">{{ group.label }}</span>
            <span class="muted">{{ groupEnabledCount(group) }} of {{ group.tools.length }}</span>
          </div>
          <div style="display: grid; grid-template-columns: repeat(auto-fill, minmax(220px, 1fr)); gap: 6px 16px; padding: 10px 12px 10px 34px">
            <label v-for="tool in group.tools" :key="tool" class="tgl">
              <input type="checkbox" class="chk" :checked="isEnabled(tool)" @change="setEnabled(tool, !isEnabled(tool))" />
              <span class="mono">{{ tool }}</span>
            </label>
          </div>
        </template>
      </div>
      <div class="help" style="margin-top: 0">Tools are provided by the Hazor MCP server. Disabled tools are never offered to the model.</div>
    </Group>

    <TestPromptGroup :editor="editor" />
  </template>
</template>
