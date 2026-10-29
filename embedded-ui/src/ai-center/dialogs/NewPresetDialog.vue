<script setup lang="ts">
import { computed, ref } from 'vue'

import Dialog from '../components/Dialog.vue'
import SelectField from '../components/SelectField.vue'
import type { ProviderType } from '../services/contract'
import type { PresetEditor } from '../services/usePresetEditor'

const props = defineProps<{ editor: PresetEditor; kindLabel: string }>()
const emit = defineEmits<{ close: [] }>()

const name = ref('')
const description = ref('')
const providerType = ref<ProviderType>('local')
const creating = ref(false)

const canCreate = computed(() => name.value.trim() !== '' && !creating.value)

async function onCreate(): Promise<void> {
  creating.value = true
  try {
    await props.editor.create(name.value.trim(), description.value.trim(), providerType.value)
    emit('close')
  } finally {
    creating.value = false
  }
}
</script>

<template>
  <Dialog :title="`New ${kindLabel} Preset`" @close="emit('close')">
    <div>
      <label class="flbl" for="np-name">Preset name</label>
      <input id="np-name" v-model.trim="name" class="in" />
    </div>
    <div>
      <label class="flbl" for="np-desc">Description <span class="muted">(optional)</span></label>
      <input id="np-desc" v-model.trim="description" class="in" />
    </div>
    <div>
      <label class="flbl" for="np-provider">Provider Type</label>
      <SelectField
        id="np-provider"
        v-model="providerType"
        :options="[
          { value: 'local', label: 'Local' },
          { value: 'openai', label: 'OpenAI' },
          { value: 'anthropic', label: 'Anthropic' },
          { value: 'google', label: 'Google' },
          { value: 'custom', label: 'Custom' },
        ]"
      />
      <div class="help">Base URL is prefilled for the provider. You can change everything after creating.</div>
    </div>
    <template #footer>
      <button type="button" class="btn ghost" @click="emit('close')">Cancel</button>
      <button type="button" class="btn pri" :disabled="!canCreate" @click="onCreate">Create Preset</button>
    </template>
  </Dialog>
</template>
