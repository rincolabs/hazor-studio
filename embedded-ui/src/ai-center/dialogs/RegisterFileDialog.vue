<script setup lang="ts">
import { computed, ref } from 'vue'

import Dialog from '../components/Dialog.vue'
import SegmentedControl from '../components/SegmentedControl.vue'
import SelectField from '../components/SelectField.vue'
import type { SdModelCategory, SdTextEncoderRole } from '../services/contract'
import { useSdModels } from '../services/useSdModels'

const emit = defineEmits<{ close: [] }>()

const { registerFile, browseForFile } = useSdModels()

const path = ref('')
const category = ref<SdModelCategory>('checkpoint')
const encoderRole = ref<SdTextEncoderRole>('auto')
const id = ref('')
const registering = ref(false)

const categoryOptions: Array<{ value: SdModelCategory; label: string }> = [
  { value: 'checkpoint', label: 'Checkpoint' },
  { value: 'diffusionModel', label: 'Diffusion Model / UNet' },
  { value: 'textEncoder', label: 'Text Encoder' },
  { value: 'visionEncoder', label: 'Vision Encoder' },
  { value: 'vae', label: 'VAE' },
  { value: 'lora', label: 'LoRA' },
  { value: 'embedding', label: 'Embedding / Textual Inversion' },
  { value: 'controlNet', label: 'ControlNet' },
  { value: 'ipAdapter', label: 'IP-Adapter' },
  { value: 'photoMaker', label: 'PhotoMaker' },
  { value: 'pulid', label: 'PuLID' },
  { value: 'identityEmbedding', label: 'Identity Embedding (PhotoMaker / PuLID)' },
  { value: 'upscaler', label: 'Upscaler (ESRGAN)' },
  { value: 'adetailer', label: 'ADetailer Detector' },
]

const canRegister = computed(() => path.value.trim() !== '' && id.value.trim() !== '' && !registering.value)

function basenameWithoutExtension(filePath: string): string {
  const name = filePath.split(/[\\/]/).pop() ?? filePath
  const dot = name.lastIndexOf('.')
  return dot > 0 ? name.slice(0, dot) : name
}

async function onBrowse(): Promise<void> {
  const chosen = await browseForFile()
  if (chosen) {
    path.value = chosen
    if (!id.value) {
      id.value = basenameWithoutExtension(chosen)
    }
  }
}

async function onRegister(): Promise<void> {
  registering.value = true
  try {
    await registerFile(path.value.trim(), category.value, category.value === 'textEncoder' ? encoderRole.value : 'auto', id.value.trim())
    emit('close')
  } finally {
    registering.value = false
  }
}
</script>

<template>
  <Dialog title="Register Model File" @close="emit('close')">
    <div>
      <label class="flbl" for="rf-path">File</label>
      <div class="ctl" style="flex-wrap: nowrap">
        <input id="rf-path" v-model.trim="path" class="in mono" />
        <button type="button" class="btn" @click="onBrowse">Browse…</button>
      </div>
    </div>
    <div>
      <label class="flbl" for="rf-cat">Category</label>
      <SelectField id="rf-cat" v-model="category" :options="categoryOptions" />
    </div>
    <div v-if="category === 'textEncoder'">
      <span class="flbl">Text Encoder Role <span class="muted">(optional)</span></span>
      <SegmentedControl
        v-model="encoderRole"
        aria-label="Text encoder role"
        :options="[
          { value: 'auto', label: 'Auto' },
          { value: 'clipL', label: 'CLIP-L' },
          { value: 'clipG', label: 'CLIP-G' },
          { value: 't5xxl', label: 'T5-XXL' },
          { value: 'llm', label: 'LLM' },
          { value: 'llmVision', label: 'LLM Vision' },
          { value: 'clipVision', label: 'CLIP Vision' },
        ]"
      />
    </div>
    <div>
      <label class="flbl" for="rf-id">Name / ID</label>
      <input id="rf-id" v-model.trim="id" class="in mono" />
    </div>
    <template #footer>
      <button type="button" class="btn ghost" @click="emit('close')">Cancel</button>
      <button type="button" class="btn pri" :disabled="!canRegister" @click="onRegister">Register</button>
    </template>
  </Dialog>
</template>
