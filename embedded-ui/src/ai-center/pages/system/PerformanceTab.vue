<script setup lang="ts">
import Group from '../../components/Group.vue'
import NumberField from '../../components/NumberField.vue'
import SegmentedControl from '../../components/SegmentedControl.vue'
import Toggle from '../../components/Toggle.vue'
import FooterActionBar from '../../layout/FooterActionBar.vue'
import type { WorkingResolutionPolicy } from '../../services/contract'
import { useRuntime } from '../../services/useRuntime'

const { draft, dirty, saving, apply, revert } = useRuntime()

const resolutionPolicies: Array<{ value: WorkingResolutionPolicy; title: string; desc: string }> = [
  { value: 'auto', title: 'Auto', desc: 'Chooses a working size per model and image.' },
  { value: 'preserveDocument', title: 'Preserve Document Resolution', desc: 'Full quality; uses the most memory.' },
  { value: 'limitLongestSide', title: 'Limit Longest Side', desc: 'Downscale for inference, then map results back to the document.' },
]
</script>

<template>
  <div class="body">
    <div v-if="draft" style="display: grid; grid-template-columns: repeat(auto-fit, minmax(380px, 1fr)); gap: 14px; align-items: start">
      <Group title="Processing">
        <div class="row">
          <label class="lbl" for="ct">CPU Threads</label>
          <div>
            <NumberField v-model="draft.cpuThreadCount" unit="Auto" :min="0" :max="256" aria-label="CPU Threads" />
            <div class="help">0 = Auto. Range 0–256.</div>
          </div>
        </div>
        <div class="row">
          <label class="lbl" for="mj">Maximum Concurrent AI Jobs</label>
          <div>
            <NumberField v-model="draft.maxConcurrentAiJobs" :min="1" :max="8" aria-label="Maximum Concurrent AI Jobs" />
            <div class="help">1–8. Heavy jobs beyond this limit wait in a queue.</div>
          </div>
        </div>
        <div class="row">
          <label class="lbl" for="ml">Maximum Loaded Models</label>
          <div>
            <NumberField v-model="draft.maxLoadedModels" :min="1" :max="8" aria-label="Maximum Loaded Models" />
            <div class="help">1–8. The least recently used model is unloaded first.</div>
          </div>
        </div>
      </Group>

      <Group title="Memory">
        <div class="row">
          <label class="lbl" for="gm">GPU Memory Budget</label>
          <div>
            <NumberField v-model="draft.maxGpuMemoryMB" unit="MB" :min="0" :max="131072" :step="256" aria-label="GPU Memory Budget" />
            <div class="help">0 = Auto. 0–131072 MB in steps of 256.</div>
          </div>
        </div>
        <div class="row">
          <label class="lbl" for="cm">CPU Memory Budget</label>
          <div>
            <NumberField v-model="draft.maxCpuMemoryMB" unit="MB" :min="0" :max="131072" :step="256" aria-label="CPU Memory Budget" />
            <div class="help">0 = Auto. 0–131072 MB in steps of 256.</div>
          </div>
        </div>
      </Group>

      <Group title="Embedding Cache">
        <div class="row" style="align-items: center">
          <span class="lbl" style="padding: 0">Enable Embedding Cache</span>
          <Toggle v-model="draft.embeddingCacheEnabled" label="Enable Embedding Cache">
            <span class="muted">{{ draft.embeddingCacheEnabled ? 'On' : 'Off' }}</span>
          </Toggle>
        </div>
        <p class="grp-desc">Keeps image embeddings per document so repeated selections on the same image are instant.</p>
        <div class="row" :style="!draft.embeddingCacheEnabled ? 'opacity:.75' : undefined">
          <label class="lbl" for="cd" :style="!draft.embeddingCacheEnabled ? 'color:#9c9e9f' : undefined">Maximum Cached Documents</label>
          <div>
            <NumberField v-model="draft.maxCachedDocuments" :min="0" :max="32" :disabled="!draft.embeddingCacheEnabled" aria-label="Maximum Cached Documents" />
            <div class="help">0–32 documents.</div>
          </div>
        </div>
        <div class="row" :style="!draft.embeddingCacheEnabled ? 'opacity:.75' : undefined">
          <label class="lbl" for="ce" :style="!draft.embeddingCacheEnabled ? 'color:#9c9e9f' : undefined">Maximum Cached Embedding Memory</label>
          <div>
            <NumberField v-model="draft.maxCachedEmbeddingsMB" unit="MB" :min="0" :max="131072" :step="128" :disabled="!draft.embeddingCacheEnabled" aria-label="Maximum Cached Embedding Memory" />
            <div class="help">0 = Auto. 0–131072 MB in steps of 128.</div>
          </div>
        </div>
        <div v-if="!draft.embeddingCacheEnabled" class="muted" style="display: flex; gap: 6px; align-items: center; font-size: 11px">
          <svg class="i" viewBox="0 0 24 24" aria-hidden="true"><rect x="5" y="11" width="14" height="10" rx="1" /><path d="M8 11V7a4 4 0 0 1 8 0v4" /></svg>
          Enable Embedding Cache to edit these limits.
        </div>
      </Group>

      <Group title="Working Resolution">
        <div class="row">
          <span class="lbl">Resolution Policy</span>
          <div style="display: flex; flex-direction: column; gap: 6px" role="radiogroup" aria-label="Resolution policy">
            <label v-for="policy in resolutionPolicies" :key="policy.value" class="radio-card" :class="{ on: draft.workingResolution === policy.value }">
              <input type="radio" name="rp" :checked="draft.workingResolution === policy.value" @change="draft.workingResolution = policy.value" />
              <div>
                <div class="rc-t">{{ policy.title }}</div>
                <div class="rc-s">{{ policy.desc }}</div>
              </div>
            </label>
          </div>
        </div>
        <div class="row">
          <span class="lbl">Longest Side Limit</span>
          <div>
            <SegmentedControl
              :model-value="String(draft.workingResolutionLongestSide)"
              :options="[
                { value: '1024', label: '1024 px' },
                { value: '1536', label: '1536 px' },
                { value: '2048', label: '2048 px' },
                { value: '3072', label: '3072 px' },
              ]"
              aria-label="Longest side limit"
              @update:model-value="draft.workingResolutionLongestSide = Number($event)"
            />
            <div class="help">Only used when the policy is Limit Longest Side.</div>
          </div>
        </div>
      </Group>
    </div>
  </div>

  <FooterActionBar :dirty="dirty" dirty-label="Unsaved changes" :saving="saving" @apply="apply" @revert="revert" />
</template>
