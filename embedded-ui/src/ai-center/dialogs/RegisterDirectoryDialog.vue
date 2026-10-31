<script setup lang="ts">
import { computed, ref } from 'vue'

import Dialog from '../components/Dialog.vue'
import SelectField from '../components/SelectField.vue'
import Toggle from '../components/Toggle.vue'
import type { SdModelCategory } from '../services/contract'
import { useSdModels } from '../services/useSdModels'

const emit = defineEmits<{ close: [] }>()

const { registerDirectory, browseForDirectory } = useSdModels()

const path = ref('')
const category = ref<SdModelCategory>('lora')
const recursive = ref(true)
const registering = ref(false)

const categoryOptions: Array<{ value: SdModelCategory; label: string }> = [
  { value: 'lora', label: 'LoRA' },
  { value: 'checkpoint', label: 'Checkpoint' },
  { value: 'diffusionModel', label: 'Diffusion Model / UNet' },
  { value: 'textEncoder', label: 'Text Encoder' },
  { value: 'visionEncoder', label: 'Vision Encoder' },
  { value: 'vae', label: 'VAE' },
  { value: 'embedding', label: 'Embedding / Textual Inversion' },
  { value: 'controlNet', label: 'ControlNet' },
  { value: 'ipAdapter', label: 'IP-Adapter' },
  { value: 'photoMaker', label: 'PhotoMaker' },
  { value: 'pulid', label: 'PuLID' },
  { value: 'identityEmbedding', label: 'Identity Embedding (PhotoMaker / PuLID)' },
  { value: 'upscaler', label: 'Upscaler (ESRGAN)' },
  { value: 'adetailer', label: 'ADetailer Detector' },
]

const canRegister = computed(() => path.value.trim() !== '' && !registering.value)

async function onBrowse(): Promise<void> {
  const chosen = await browseForDirectory()
  if (chosen) {
    path.value = chosen
  }
}

async function onRegister(): Promise<void> {
  registering.value = true
  try {
    await registerDirectory(path.value.trim(), category.value, 'auto', recursive.value)
    emit('close')
  } finally {
    registering.value = false
  }
}
</script>

<template>
  <Dialog title="Register Directory" @close="emit('close')">
    <div>
      <label class="flbl" for="rd-path">Directory</label>
      <div class="ctl" style="flex-wrap: nowrap">
        <input id="rd-path" v-model.trim="path" class="in mono" />
        <button type="button" class="btn" @click="onBrowse">Browse…</button>
      </div>
    </div>
    <div>
      <label class="flbl" for="rd-cat">Category</label>
      <SelectField id="rd-cat" v-model="category" :options="categoryOptions" />
      <div class="help">Every model file in this folder is registered with this category.</div>
    </div>
    <Toggle v-model="recursive" label="Scan subfolders recursively">
      <div><div class="strong" style="font-weight: 500">Scan subfolders (recursive)</div></div>
    </Toggle>
    <template #footer>
      <button type="button" class="btn ghost" @click="emit('close')">Cancel</button>
      <button type="button" class="btn pri" :disabled="!canRegister" @click="onRegister">Register Directory</button>
    </template>
  </Dialog>
</template>
