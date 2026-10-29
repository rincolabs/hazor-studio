/**
 * Current section/subsection, shared by the whole page. A composable rather
 * than a router: five sections and a handful of tabs each don't need
 * history/URL matching, just one place that says what is showing.
 */
import { ref } from 'vue'

import type { SectionId } from './contract'

import { getClient } from './client'

const section = ref<SectionId>('overview')
const subsection = ref('')
let started = false

function start(): void {
  if (started) {
    return
  }
  started = true
  void getClient().then(async (client) => {
    client.on('navigationRequested', (nextSection, nextSubsection) => {
      section.value = nextSection
      subsection.value = nextSubsection
    })
    // AiCenterWindow::openSection() passed a deep link for this very first load;
    // every later "open to a different section" arrives as the signal above,
    // because the page stays alive and only hides between uses.
    const target = await client.initialTarget()
    if (target) {
      section.value = target.section
      subsection.value = target.subsection
    }
  })
}

export function useNavigation() {
  start()

  function navigate(nextSection: SectionId, nextSubsection = ''): void {
    section.value = nextSection
    subsection.value = nextSubsection
  }

  return { section, subsection, navigate }
}
