<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue'
import { closeMenu, menuState } from '../../composables/useMenu'
import MenuPanel from './MenuPanel.vue'
import MenuSearch from './MenuSearch.vue'

const query = ref('')
const path = ref<number[]>([])
const depth = ref(1)
const panelEls = ref<Array<HTMLElement | null>>([])
const pos = reactive<Array<{ left: number; top: number; ready: boolean }>>([])

const MARGIN = 6

const rootItems = computed(() => {
  const q = query.value.trim().toLowerCase()
  if (!menuState.search || !q) return menuState.items
  const hits = menuState.searchItems.filter(
    (it) => it.label.toLowerCase().includes(q) || (it.hint ?? '').toLowerCase().includes(q),
  )
  hits.sort(
    (a, b) => (b.label.toLowerCase().startsWith(q) ? 1 : 0) - (a.label.toLowerCase().startsWith(q) ? 1 : 0),
  )
  return hits.length ? hits : [{ empty: true, label: `No nodes match "${query.value.trim()}"` }]
})

const levels = computed(() => {
  const out = [rootItems.value]
  for (let l = 0; l < depth.value - 1; l += 1) {
    const it = out[l]?.[path.value[l] as number]
    if (!it?.children) break
    out.push(it.children)
  }
  return out
})

const selectable = (it: (typeof menuState.items)[number] | undefined): boolean =>
  Boolean(it && !it.separator && !it.empty && !it.disabled)
function firstSelectable(items: typeof menuState.items, from = 0, dir = 1): number {
  const n = items.length
  for (let k = 0; k < n; k += 1) {
    const i = (((from + dir * k) % n) + n) % n
    if (selectable(items[i])) return i
  }
  return -1
}

function reset(): void {
  query.value = ''
  depth.value = 1
  const checked = menuState.items.findIndex((i) => i.checked)
  path.value = [menuState.search ? -1 : checked >= 0 ? checked : firstSelectable(menuState.items)]
}

watch(() => menuState.id, () => {
  if (menuState.open) reset()
})
watch(query, () => {
  depth.value = 1
  path.value = [query.value.trim() ? firstSelectable(rootItems.value) : -1]
})

function setLevel(level: number, index: number, openChildren: boolean): void {
  const p = path.value.slice(0, level + 1)
  p[level] = index
  const item = levels.value[level]?.[index]
  if (openChildren && item?.children) {
    p[level + 1] = -1
    depth.value = level + 2
  } else {
    depth.value = level + 1
  }
  path.value = p
}

function onEnter(level: number, i: number): void {
  setLevel(level, i, true)
}

function onSelect(level: number, i: number): void {
  const item = levels.value[level]?.[i]
  if (!selectable(item)) return
  if (item?.children) {
    setLevel(level, i, true)
    const p = path.value.slice()
    p[level + 1] = firstSelectable(item.children)
    path.value = p
    return
  }
  closeMenu()
  item?.action?.()
}

function onKey(e: KeyboardEvent): void {
  if (!menuState.open) return
  const last = levels.value.length - 1
  const items = levels.value[last] ?? []
  const cur = path.value[last] ?? -1
  const inSearch = (e.target as HTMLElement)?.tagName === 'INPUT'

  switch (e.key) {
    case 'ArrowDown':
    case 'ArrowUp': {
      e.preventDefault()
      const dir = e.key === 'ArrowDown' ? 1 : -1
      const start = cur < 0 ? (dir > 0 ? 0 : items.length - 1) : cur + dir
      const p = path.value.slice(0, last + 1)
      p[last] = firstSelectable(items, start, dir)
      path.value = p
      depth.value = last + 1
      break
    }
    case 'ArrowRight': {
      if (inSearch && query.value) return
      const item = items[cur]
      if (item?.children) {
        e.preventDefault()
        const p = path.value.slice(0, last + 1)
        p[last + 1] = firstSelectable(item.children)
        path.value = p
        depth.value = last + 2
      }
      break
    }
    case 'ArrowLeft': {
      if (inSearch && query.value) return
      if (last > 0) {
        e.preventDefault()
        depth.value = last
        path.value = path.value.slice(0, last)
      }
      break
    }
    case 'Enter':
      e.preventDefault()
      if (cur >= 0) onSelect(last, cur)
      break
    case 'Escape':
      e.preventDefault()
      closeMenu()
      break
    case 'Tab':
      e.preventDefault()
      break
  }
}

async function place(): Promise<void> {
  if (!menuState.open) return
  await nextTick()
  const W = window.innerWidth
  const H = window.innerHeight
  const flipped: boolean[] = []
  for (let l = 0; l < levels.value.length; l += 1) {
    const el = panelEls.value[l]
    if (!el) continue
    const w = el.offsetWidth
    const h = el.offsetHeight
    let left: number
    let top: number
    if (l === 0) {
      left = menuState.x
      top = menuState.y
    } else {
      const parent = panelEls.value[l - 1]
      const row = parent?.querySelector<HTMLElement>(`[data-idx="${path.value[l - 1]}"]`)
      if (!parent || !row) continue
      const pr = parent.getBoundingClientRect()
      const rr = row.getBoundingClientRect()
      const rightX = pr.right - 2
      const leftX = pr.left - w + 2
      const fitsRight = rightX + w <= W - MARGIN
      const fitsLeft = leftX >= MARGIN
      flipped[l] = flipped[l - 1] ? fitsLeft || !fitsRight : !fitsRight && fitsLeft
      left = flipped[l] ? leftX : rightX
      top = rr.top - 5
    }
    left = Math.max(MARGIN, Math.min(left, W - w - MARGIN))
    top = Math.max(MARGIN, Math.min(top, H - h - MARGIN))
    pos[l] = { left, top, ready: true }
  }
  pos.length = levels.value.length
}

watch(
  () => [menuState.open, menuState.id, levels.value.length, path.value.join(','), query.value],
  () => {
    for (let l = 0; l < levels.value.length; l += 1) {
      if (!pos[l]) pos[l] = { left: -9999, top: -9999, ready: false }
    }
    void place()
  },
  { flush: 'post' },
)

function onResize(): void {
  if (menuState.open) void place()
}
onMounted(() => {
  window.addEventListener('keydown', onKey, true)
  window.addEventListener('resize', onResize)
})
onBeforeUnmount(() => {
  window.removeEventListener('keydown', onKey, true)
  window.removeEventListener('resize', onResize)
})

function panelStyle(l: number): Record<string, string> {
  const p = pos[l]
  return p?.ready
    ? { left: `${p.left}px`, top: `${p.top}px` }
    : { left: '-9999px', top: '-9999px', visibility: 'hidden' }
}

function setPanelEl(l: number, el: unknown): void {
  panelEls.value[l] = (el as { $el?: HTMLElement } | HTMLElement | null)
    ? ((el as { $el?: HTMLElement }).$el ?? (el as HTMLElement))
    : null
}
</script>

<template>
  <Teleport to="body">
    <div
      v-if="menuState.open"
      class="mn-backdrop"
      @mousedown="closeMenu"
      @contextmenu.prevent="closeMenu"
      @wheel="closeMenu"
    >
      <div
        v-for="(items, l) in levels"
        :key="`${l}:${menuState.id}`"
        :ref="(el) => setPanelEl(l, el)"
        class="mn-pos"
        :style="panelStyle(l)"
      >
        <MenuPanel
          :items="items"
          :level="l"
          :active-index="l === levels.length - 1 || (path[l + 1] ?? -1) < 0 ? path[l] ?? -1 : -1"
          :open-index="l < levels.length - 1 ? (path[l] ?? -1) : -1"
          :query="l === 0 ? query : ''"
          :title="l === 0 ? menuState.title : ''"
          :min-width="l === 0 ? menuState.minWidth : 0"
          @enter="onEnter(l, $event)"
          @select="onSelect(l, $event)"
        >
          <template v-if="l === 0 && menuState.title" #title-extra>
            <span v-if="menuState.search" class="kbd">Shift A</span>
          </template>
          <template v-if="l === 0 && menuState.search" #top>
            <MenuSearch v-model="query" placeholder="Search nodes…" />
            <div v-if="!query" class="mn-sep-top" />
          </template>
        </MenuPanel>
      </div>
    </div>
  </Teleport>
</template>

<style scoped>
.mn-backdrop {
  position: fixed;
  inset: 0;
  z-index: 1000;
}
.mn-pos {
  position: fixed;
}
.mn-sep-top {
  height: 1px;
  margin: 0 2px 4px;
  background: var(--bg-4);
}
</style>
