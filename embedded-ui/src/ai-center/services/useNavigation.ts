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
// Bumped on every navigation request, including one that repeats the current
// section and subsection. Pages watch it instead of `subsection` alone: the
// second "open Models > Stable Diffusion" has to land on that tab even when
// the user has since clicked over to Vision, and comparing values cannot tell
// that apart from no navigation at all.
const revision = ref(0)
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
      revision.value += 1
    })
    // AiCenterWindow::openSection() passed a deep link for this very first load;
    // every later "open to a different section" arrives as the signal above,
    // because the page stays alive and only hides between uses.
    const target = await client.initialTarget()
    if (target) {
      section.value = target.section
      subsection.value = target.subsection
      revision.value += 1
    }
  })
}

export function useNavigation() {
  start()

  function navigate(nextSection: SectionId, nextSubsection = ''): void {
    section.value = nextSection
    subsection.value = nextSubsection
    revision.value += 1
  }

  return { section, subsection, revision, navigate }
}
