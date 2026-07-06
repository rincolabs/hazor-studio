#pragma once

#include <QString>
#include <QColor>

// Single source of truth for QScrollBar QSS.
//
// Qt has a sharp edge here: as soon as ANY scrollbar subcontrol is styled via
// QSS, Qt stops drawing the whole scrollbar natively and only renders the
// subcontrols the sheet specifies. Any left unstyled fall back to the native
// style — and on Windows (windowsvista) that fallback paints the dotted focus /
// track artifacts users perceive as "unstyled" scrollbars.
//
// Every scrollbar in the app (the global sheet in ThemeBuilder and the per-panel
// sheets) must therefore emit a COMPLETE set of subcontrol rules, including the
// track (add-page/sub-page) and the arrows. Centralizing that here keeps the
// rules identical everywhere and prevents the duplication that let panels drift
// out of sync with the global sheet.

struct ScrollBarQssOptions {
    int thickness = 10;      // width (vertical) / height (horizontal), px
    int minLength = 20;      // min-height (vertical) / min-width (horizontal), px
    int borderRadius = 6;    // handle corner radius, px
    int handleMargin = 0;    // margin around the handle, px
    bool vertical = true;    // emit vertical rules
    bool horizontal = true;  // emit horizontal rules
    // When true, the non-emitted orientation is explicitly collapsed to 0 so a
    // stray scrollbar of that orientation never shows the native fallback.
    bool hideOtherOrientation = false;
};

// Build the full QScrollBar QSS block. `track` is the groove/background color,
// `handle` the draggable thumb color.
QString scrollBarQss(const QColor& track,
                     const QColor& handle,
                     const ScrollBarQssOptions& opts = {});
