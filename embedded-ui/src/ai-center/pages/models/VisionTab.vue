<script setup lang="ts">
import { computed, ref } from 'vue'

import Dialog from '../../components/Dialog.vue'
import Group from '../../components/Group.vue'
import Icon from '../../components/Icon.vue'
import type { ModelDescriptor } from '../../services/contract'
import { useModelsVision } from '../../services/useModelsVision'

const { state, setDefaultModel, removeInstalledModel, showModelFiles, openModelLicense } = useModelsVision()

const expanded = ref<Set<string>>(new Set())
const removing = ref<ModelDescriptor | null>(null)

const originLabel: Record<ModelDescriptor['origin'], string> = {
  bundled: 'Bundled',
  downloaded: 'Downloaded',
  custom: 'Custom',
}

const totals = computed(() => {
  const models = state.value?.taskGroups.flatMap((g) => g.models) ?? []
  return { total: models.length, valid: models.filter((m) => m.isValid).length, invalid: models.filter((m) => !m.isValid).length }
})

function toggle(id: string): void {
  const next = new Set(expanded.value)
  if (next.has(id)) next.delete(id)
  else next.add(id)
  expanded.value = next
}

async function confirmRemove(): Promise<void> {
  if (!removing.value) return
  await removeInstalledModel(removing.value.id)
  removing.value = null
}
</script>

<template>
  <div class="body">
    <div style="display: flex; flex-wrap: wrap; gap: 10px; align-items: center">
      <span class="strong">ONNX models for image analysis and editing</span>
      <span class="muted">{{ totals.total }} models · {{ totals.valid }} valid · {{ totals.invalid }} invalid</span>
      <span style="flex: 1" />
      <span v-if="state && !state.manifestConfigured" class="muted" style="display: inline-flex; align-items: center; gap: 6px">
        <Icon name="info" />Remote downloads are not configured in this build.
      </span>
    </div>

    <Group v-for="group in state?.taskGroups ?? []" :key="group.task" :title="group.taskLabel">
      <template v-if="!group.exposedInUi" #icon>
        <span class="bdg sq" style="font-weight: 500">Not yet exposed in the app UI</span>
      </template>
      <template #meta>
        {{ group.models.length }} models · Default: <span class="strong">{{ group.models.find((m) => m.id === group.defaultModelId)?.displayName ?? '—' }}</span>
      </template>
      <div>
        <div
          v-for="model in group.models"
          :key="model.id"
          class="mrow"
          :class="{ open: expanded.has(model.id), bad: !model.isValid }"
        >
          <div>
            <div class="m-name">
              {{ model.displayName }}
              <span v-if="model.isDefault" class="bdg info sq"><Icon name="star" style="width: 11px; height: 11px" />Default</span>
            </div>
            <div class="m-desc">{{ model.description }}</div>
            <div v-if="!model.isValid" class="field-err"><Icon name="error" />Invalid: {{ model.statusReason }}</div>
          </div>
          <div class="m-tags">
            <span class="bdg sq">{{ originLabel[model.origin] }}</span>
            <span v-if="model.quality" class="bdg sq">{{ model.quality }}</span>
            <span class="bdg" :class="model.isValid ? 'ok' : 'err'"><span class="dot" />{{ model.isValid ? 'Valid' : 'Invalid' }}</span>
            <button
              v-if="model.isValid && !model.isDefault && !expanded.has(model.id)"
              type="button"
              class="btn"
              style="height: 22px"
              @click="setDefaultModel(group.task, model.id)"
            >
              Use as Default
            </button>
            <button
              type="button"
              class="btn ghost ib"
              :aria-label="expanded.has(model.id) ? 'Hide details' : 'Show details'"
              :aria-expanded="expanded.has(model.id)"
              :title="expanded.has(model.id) ? 'Hide details' : 'Show details'"
              @click="toggle(model.id)"
            >
              <Icon :name="expanded.has(model.id) ? 'chevronUp' : 'chevronDown'" />
            </button>
          </div>

          <template v-if="expanded.has(model.id)">
            <div class="m-det">
              <div>
                <span class="flbl">Model files</span>
                <div class="mono" style="display: flex; flex-direction: column; gap: 2px; color: var(--text)">
                  <span v-for="file in model.files" :key="file">{{ file }}</span>
                </div>
              </div>
              <div class="kv" style="grid-template-columns: 70px minmax(0, 1fr)">
                <span class="k">License</span><span class="v">{{ model.licenseName || '—' }}</span>
                <span class="k">Source</span>
                <span class="v"><a v-if="model.sourceUrl" :href="model.sourceUrl" target="_blank" rel="noopener">{{ model.sourceName || model.sourceUrl }}</a><template v-else>{{ model.sourceName || '—' }}</template></span>
                <span class="k">Task</span><span class="v">{{ group.taskLabel }}</span>
              </div>
            </div>
            <div class="m-act">
              <button
                v-if="model.isValid && !model.isDefault"
                type="button"
                class="btn pri"
                @click="setDefaultModel(group.task, model.id)"
              >
                <Icon name="star" />Use as Default
              </button>
              <button v-else-if="!model.isValid" type="button" class="btn" disabled title="Invalid models can't be set as default">
                Use as Default
              </button>
              <button type="button" class="btn" @click="showModelFiles(model.id)"><Icon name="folder" />Show Files</button>
              <button v-if="model.licenseFile || model.sourceUrl" type="button" class="btn" @click="openModelLicense(model.id)">
                <Icon name="externalLink" />Open License
              </button>
              <span style="flex: 1" />
              <template v-if="!model.isValid">
                <span class="muted" style="align-self: center">Invalid models can't be set as default. Restore the missing file, then reopen this page.</span>
              </template>
              <button v-else-if="model.origin === 'downloaded'" type="button" class="btn danger" @click="removing = model">
                <Icon name="trash" />Remove
              </button>
            </div>
          </template>
        </div>
      </div>
    </Group>

    <Dialog v-if="removing" :title="`Remove “${removing.displayName}”?`" alert @close="removing = null">
      <p style="margin: 0">The downloaded files are deleted from disk. Bundled and custom models can't be removed here.</p>
      <template #footer>
        <button type="button" class="btn ghost" @click="removing = null">Cancel</button>
        <button type="button" class="btn danger-fill" @click="confirmRemove">Remove Model</button>
      </template>
    </Dialog>
  </div>
</template>
