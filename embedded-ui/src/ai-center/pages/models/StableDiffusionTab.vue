<script setup lang="ts">
import { computed, ref } from 'vue'

import EmptyState from '../../components/EmptyState.vue'
import Icon from '../../components/Icon.vue'
import SelectField from '../../components/SelectField.vue'
import RegisterDirectoryDialog from '../../dialogs/RegisterDirectoryDialog.vue'
import RegisterFileDialog from '../../dialogs/RegisterFileDialog.vue'
import type { SdModelCategory, SdModelEntry } from '../../services/contract'
import { useSdModels } from '../../services/useSdModels'

const { state, unregisterEntry, unregisterDirectory, setDefaultModel, rescan } = useSdModels()

const categoryLabel: Record<SdModelCategory, string> = {
  checkpoint: 'Checkpoint',
  diffusionModel: 'Diffusion Model / UNet',
  textEncoder: 'Text Encoder',
  visionEncoder: 'Vision Encoder',
  vae: 'VAE',
  lora: 'LoRA',
  embedding: 'Embedding',
  controlNet: 'ControlNet',
  ipAdapter: 'IP-Adapter',
  photoMaker: 'PhotoMaker',
  pulid: 'PuLID',
  identityEmbedding: 'Identity Embedding',
  upscaler: 'Upscaler (ESRGAN)',
  adetailer: 'ADetailer Detector',
}
const encoderRoleSuffix: Record<string, string> = {
  clipL: 'CLIP-L',
  clipG: 'CLIP-G',
  t5xxl: 'T5-XXL',
  llm: 'LLM',
  llmVision: 'LLM Vision',
  clipVision: 'CLIP Vision',
}
const sourceLabel: Record<SdModelEntry['source'], string> = {
  directory: 'Directory',
  registeredFile: 'Registered file',
}

type CategoryFilter = 'all' | SdModelCategory
const categoryFilter = ref<CategoryFilter>('all')
const categoryFilterOptions: Array<{ value: CategoryFilter; label: string }> = [
  { value: 'all', label: 'All categories' },
  ...(Object.entries(categoryLabel) as Array<[SdModelCategory, string]>).map(([value, label]) => ({ value, label })),
]
const showRegisterFile = ref(false)
const showRegisterDirectory = ref(false)
const rescanning = ref(false)

function categoryText(entry: SdModelEntry): string {
  if (entry.category === 'textEncoder' && entry.encoderRole !== 'auto') {
    return `${categoryLabel.textEncoder} · ${encoderRoleSuffix[entry.encoderRole]}`
  }
  return categoryLabel[entry.category]
}

const checkpointOptions = computed(
  () => state.value?.entries.filter((e) => e.category === 'checkpoint' && e.available) ?? [],
)

const filteredEntries = computed(() => {
  const entries = state.value?.entries ?? []
  return categoryFilter.value === 'all' ? entries : entries.filter((e) => e.category === categoryFilter.value)
})

async function onDefaultChange(id: string): Promise<void> {
  await setDefaultModel(id)
}

async function onRescan(): Promise<void> {
  rescanning.value = true
  try {
    await rescan()
  } finally {
    rescanning.value = false
  }
}
</script>

<template>
  <div class="body">
    <div style="display: flex; flex-wrap: wrap; gap: 8px; align-items: center">
      <button type="button" class="btn pri" @click="showRegisterFile = true">
        <Icon name="file" />Add File…
      </button>
      <button type="button" class="btn" @click="showRegisterDirectory = true">
        <Icon name="folderPlus" />Add Directory…
      </button>
      <button type="button" class="btn" :disabled="rescanning" @click="onRescan">
        <Icon name="refresh" :class="{ spin: rescanning }" />Rescan Directories
      </button>
      <span v-if="state?.lastScanAt" class="muted" style="margin-left: 4px">Last scan {{ state.lastScanAt }} · {{ state.entries.length }} models found</span>
      <span style="flex: 1" />
      <label class="lbl" for="dc" style="white-space: nowrap">Default Checkpoint</label>
      <SelectField
        id="dc"
        :model-value="state?.defaultModelId ?? ''"
        mono
        style="width: 240px"
        :options="checkpointOptions.map((e) => ({ value: e.id, label: e.id }))"
        @update:model-value="onDefaultChange"
      />
    </div>

    <EmptyState v-if="state && state.entries.length === 0" icon="sdNode" title="No Stable Diffusion models registered">
      <div style="max-width: 460px">
        Register checkpoints, UNet models, text encoders, VAEs, LoRAs, ControlNets, IP-Adapters, PhotoMaker,
        PuLID and upscaler models so Neural Flow can use them. Add a single file, or a whole folder such as an
        existing ComfyUI models directory.
      </div>
      <div style="display: flex; gap: 8px">
        <button type="button" class="btn pri" @click="showRegisterFile = true">Add File…</button>
        <button type="button" class="btn" @click="showRegisterDirectory = true">Add Directory…</button>
      </div>
    </EmptyState>

    <template v-else>
    <div v-if="state" style="display: grid; grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); gap: 8px">
      <div v-for="cat in (Object.keys(categoryLabel) as SdModelCategory[])" :key="cat" class="stat">
        <div class="stat-n">{{ state.countsByCategory[cat] ?? 0 }}</div>
        <div class="stat-l">{{ categoryLabel[cat] }}</div>
      </div>
    </div>

    <div class="bn info">
      <Icon name="info" />
      <div>Showing effective registrations from all sources (registered files, registered directories and environment variables).</div>
    </div>

    <section style="display: flex; flex-direction: column; gap: 8px" aria-labelledby="rm">
      <div style="display: flex; align-items: center; gap: 8px">
        <h2 id="rm" style="margin: 0; font-size: 12.5px; color: var(--text); font-weight: 600">Registered Models</h2>
        <span class="muted">{{ state?.entries.length ?? 0 }}</span>
        <span style="flex: 1" />
        <SelectField
          v-model="categoryFilter"
          style="width: 200px"
          aria-label="Filter by category"
          :options="categoryFilterOptions"
        />
      </div>
      <div class="tblw">
        <table class="tbl">
          <thead><tr><th>Name / ID</th><th>Category</th><th>File path</th><th>Source</th><th>Status</th><th><span style="position: absolute; left: -9999px">Actions</span></th></tr></thead>
          <tbody>
            <tr v-for="entry in filteredEntries" :key="entry.id" :class="{ sel: entry.isDefault, bad: !entry.available }">
              <td>
                <span class="strong mono">{{ entry.id }}</span>
                <span v-if="entry.isDefault" class="bdg info sq" style="margin-left: 4px">Default</span>
              </td>
              <td>{{ categoryText(entry) }}</td>
              <td class="mono">{{ entry.path }}</td>
              <td>{{ sourceLabel[entry.source] }}</td>
              <td><span class="bdg" :class="entry.available ? 'ok' : 'err'"><span class="dot" />{{ entry.available ? 'Available' : 'File not found' }}</span></td>
              <td>
                <button
                  v-if="entry.source === 'registeredFile'"
                  type="button"
                  class="btn ghost ib danger"
                  :aria-label="`Remove registration ${entry.id}`"
                  title="Remove Registration"
                  @click="unregisterEntry(entry.id)"
                >
                  <Icon name="trash" />
                </button>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
      <div class="help" style="margin-top: 0">Models found by a directory scan are managed through their directory.</div>
    </section>

    <section style="display: flex; flex-direction: column; gap: 8px" aria-labelledby="rd">
      <div style="display: flex; align-items: center; gap: 8px">
        <h2 id="rd" style="margin: 0; font-size: 12.5px; color: var(--text); font-weight: 600">Registered Directories</h2>
        <span class="muted">{{ state?.directories.length ?? 0 }}</span>
      </div>
      <div class="tblw">
        <table class="tbl">
          <thead><tr><th>Directory path</th><th>Category</th><th>Recursive</th><th>Models found</th><th>Status</th><th><span style="position: absolute; left: -9999px">Actions</span></th></tr></thead>
          <tbody>
            <tr v-for="dir in state?.directories ?? []" :key="dir.path" :class="{ bad: !dir.available }">
              <td class="mono strong">{{ dir.path }}</td>
              <td>{{ categoryLabel[dir.category] }}</td>
              <td>{{ dir.recursive ? 'Yes' : 'No' }}</td>
              <td>{{ dir.available ? dir.modelsFound : '—' }}</td>
              <td><span class="bdg" :class="dir.available ? 'ok' : 'err'"><span class="dot" />{{ dir.available ? 'Available' : 'Directory not found' }}</span></td>
              <td>
                <button
                  type="button"
                  class="btn ghost ib"
                  :class="{ danger: !dir.available }"
                  :aria-label="`Remove directory ${dir.path}`"
                  title="Remove Registration"
                  @click="unregisterDirectory(dir.path)"
                >
                  <Icon name="trash" />
                </button>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
      <div class="help" style="margin-top: 0">Sampler, CFG scale, device, steps, prompts and LoRA weights are set on individual Neural Flow nodes, not here.</div>
    </section>
    </template>

    <RegisterFileDialog v-if="showRegisterFile" @close="showRegisterFile = false" />
    <RegisterDirectoryDialog v-if="showRegisterDirectory" @close="showRegisterDirectory = false" />
  </div>
</template>
