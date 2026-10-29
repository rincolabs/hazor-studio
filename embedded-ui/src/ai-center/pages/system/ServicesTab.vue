<script setup lang="ts">
import Group from '../../components/Group.vue'
import Icon from '../../components/Icon.vue'
import NumberField from '../../components/NumberField.vue'
import Toggle from '../../components/Toggle.vue'
import FooterActionBar from '../../layout/FooterActionBar.vue'
import { useMcp } from '../../services/useMcp'

const { draft, dirty, saving, apply, revert } = useMcp()

function onPortInput(value: number): void {
  if (draft.value) {
    draft.value.port = Math.min(65535, Math.max(1024, Math.round(value)))
  }
}
</script>

<template>
  <div class="body">
    <Group v-if="draft" title="MCP Server" style="max-width: 720px">
      <template #icon><Icon name="mcp" /></template>
      <template #meta>
        <span class="bdg" :class="draft.enabled ? 'ok' : ''">
          <span class="dot" />{{ draft.enabled ? `Listening on port ${draft.port}` : 'Disabled' }}
        </span>
      </template>
      <p class="grp-desc">
        Local tool server used by the Hazor assistant and by external AI agents (for example, desktop AI
        clients) to control Hazor Studio.
      </p>
      <div class="row" style="align-items: center">
        <span class="lbl" style="padding: 0">Enable MCP Server</span>
        <Toggle v-model="draft.enabled" label="Enable MCP Server">
          <span class="muted">{{ draft.enabled ? 'On' : 'Off' }}</span>
        </Toggle>
      </div>
      <div class="row">
        <label class="lbl" for="port">TCP Port</label>
        <div>
          <NumberField :model-value="draft.port" style="max-width: 140px" :min="1024" :max="65535" ariaLabel="TCP Port" @update:model-value="onPortInput" />
          <div class="help">1024–65535. Default 8085.</div>
        </div>
      </div>
    </Group>
  </div>

  <FooterActionBar :dirty="dirty" dirty-label="Unsaved changes" :saving="saving" @apply="apply" @revert="revert" />
</template>
