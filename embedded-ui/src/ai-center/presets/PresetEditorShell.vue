<script setup lang="ts">
import { ref } from 'vue'

import EmptyState from '../components/EmptyState.vue'
import Icon from '../components/Icon.vue'
import PresetList from '../components/PresetList.vue'
import DeletePresetDialog from '../dialogs/DeletePresetDialog.vue'
import NewPresetDialog from '../dialogs/NewPresetDialog.vue'
import UnsavedChangesDialog from '../dialogs/UnsavedChangesDialog.vue'
import type { PresetEditor } from '../services/usePresetEditor'

const props = defineProps<{ editor: PresetEditor; listLabel: string; kindLabel: string }>()

const showNew = ref(false)
const showDelete = ref(false)
const pendingSwitchTo = ref<string | null>(null)

async function onSelect(name: string): Promise<void> {
  if (name === props.editor.selectedName.value) return
  if (props.editor.dirty.value) {
    pendingSwitchTo.value = name
    return
  }
  await props.editor.select(name)
}

async function onDiscardAndSwitch(): Promise<void> {
  props.editor.discard()
  const target = pendingSwitchTo.value
  pendingSwitchTo.value = null
  if (target) {
    await props.editor.select(target)
  }
}

async function onSaveAndSwitch(): Promise<void> {
  await props.editor.save()
  const target = pendingSwitchTo.value
  pendingSwitchTo.value = null
  if (target) {
    await props.editor.select(target)
  }
}
</script>

<template>
  <PresetList
    :label="listLabel"
    :presets="editor.list.value.presets"
    :selected-name="editor.selectedName.value"
    :dirty-name="editor.dirty.value ? editor.selectedName.value : undefined"
    @select="onSelect"
    @new="showNew = true"
    @duplicate="editor.duplicate()"
    @delete="showDelete = true"
  />

  <section class="ed" aria-label="Preset configuration">
    <template v-if="editor.draft.value">
      <div class="ed-h">
        <div class="ed-fields">
          <div>
            <label class="flbl" for="pn">Preset name</label>
            <input id="pn" v-model="editor.draft.value.name" class="in" />
          </div>
          <div>
            <label class="flbl" for="pd">Description</label>
            <input id="pd" v-model="editor.draft.value.description" class="in" />
          </div>
        </div>
        <div class="ed-act">
          <span v-if="editor.dirty.value" class="bdg warn"><span class="dot" />Unsaved changes</span>
          <span v-else class="muted" style="display: inline-flex; align-items: center; gap: 5px"><Icon name="check" />All changes saved</span>
          <button v-if="editor.dirty.value" type="button" class="btn ghost" @click="editor.discard()">Discard</button>
          <button type="button" class="btn" :disabled="editor.list.value.activeName === editor.selectedName.value" @click="editor.setActive()">
            <Icon name="star" />Set Active
          </button>
          <button type="button" class="btn pri" :disabled="!editor.dirty.value || editor.saving.value" @click="editor.save()">
            <Icon name="save" />Save Changes
          </button>
        </div>
      </div>

      <slot />
    </template>
    <EmptyState v-else-if="!editor.loading.value" icon="assistants" :title="`No ${kindLabel} presets yet`">
      <div>Create a preset to connect a local or cloud model to Hazor Studio.</div>
      <button type="button" class="btn pri" @click="showNew = true"><Icon name="plus" />New Preset</button>
    </EmptyState>
  </section>

  <NewPresetDialog v-if="showNew" :editor="editor" :kind-label="kindLabel" @close="showNew = false" />
  <DeletePresetDialog v-if="showDelete" :editor="editor" @close="showDelete = false" />
  <UnsavedChangesDialog
    v-if="pendingSwitchTo"
    :preset-name="editor.selectedName.value"
    @cancel="pendingSwitchTo = null"
    @discard="onDiscardAndSwitch"
    @save-and-switch="onSaveAndSwitch"
  />
</template>
