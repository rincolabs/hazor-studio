#pragma once

#include <QVariantMap>

// The active theme as a flat map, for the HTML interfaces Hazor embeds through
// Qt WebEngine (the AI Agent panel and the Neural Flow editor).
//
// It exists so those panels share one translation of Theme into web values.
// Each of them reading ThemeManager itself would be two lists of key names that
// drift, and two panels that stop looking like the same application after a
// colour is added here — the web side of invariant 8 in AGENTS.md: no hardcoded
// colours, everything from the active Theme.
//
// Keys are stable: a frontend reads them by name, so renaming one is a
// breaking change to every interface under embedded-ui/ and src/resources/.
namespace webtheme {

QVariantMap variables();

} // namespace webtheme
