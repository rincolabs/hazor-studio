#pragma once

#include <QColor>
#include <QPointF>

#include <functional>

class CanvasView;

// ── The canvas the colour dialogs pick from ─────────────────────────────────
// ColorPickerDialog is shared by a dozen call sites (tool options, swatches,
// layer styles, Solid Color, …) and none of them knows about documents or tabs.
// MainWindow installs one resolver here in its constructor; because the
// resolver is evaluated on every call, the dialogs always reach the canvas of
// the ACTIVE tab, without every tab switch having to re-register anything.
//
// Picks are taken from the canvas itself (CanvasView::setColorPickArmed) rather
// than from a grabbed screenshot of the screen: the document's own pixels are
// exact — no zoom filtering, no colour-managed display transform, no dialog
// chrome under the cursor — the eyedropper's sample mode/size keep working, and
// no screen-capture grab is needed (Wayland would refuse one anyway).
class CanvasColorPickSource {
public:
    using Resolver = std::function<CanvasView*()>;

    // Installs (or, with a null resolver, removes) the active-canvas resolver.
    static void setResolver(Resolver resolver);

    // The canvas to pick from, or nullptr when there is no document open and no
    // host has registered a resolver.
    static CanvasView* canvas();
    static bool available() { return canvas() != nullptr; }
};
