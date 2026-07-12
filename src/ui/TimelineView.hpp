#pragma once

#include <QAbstractScrollArea>
#include <QColor>
#include <QPoint>
#include <QRect>
#include <QSet>
#include <QVector>

#include "core/LayerId.hpp"
#include "animation/AnimationTypes.hpp"

class ImageController;
class Document;

// The interactive body of the animation timeline: a frozen frame ruler on top, a
// frozen property-label gutter on the left and a scrollable keyframe grid. It is
// a pure VIEW of the document's animation model — it never keeps its own copy of
// the keyframes. Every mutation (scrubbing, selection-driven moves, deletes) goes
// through the ImageController's commands and undo/redo, exactly like the rest of
// the app. Selection is the only widget-local state, and it references model
// keyframes by (layer, property, frame) rather than duplicating their data.
class TimelineView final : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit TimelineView(QWidget* parent = nullptr);

    void setController(ImageController* controller);

    // A live reference to one model keyframe. Never owns keyframe data.
    struct KeyRef {
        LayerId layer;
        anim::Property prop;
        int frame = 0;
        bool operator==(const KeyRef& o) const {
            return layer == o.layer && prop == o.prop && frame == o.frame;
        }
    };

    // The row the toolbar single-key actions (+Key, interpolation, copy/paste,
    // new/duplicate/delete cel) operate on — the last label or keyframe clicked.
    struct RowRef {
        bool valid = false;
        bool raster = false;      // a raster-cel row rather than a property row
        LayerId layer;
        anim::Property prop = anim::Property::Opacity;
    };

    RowRef currentRow() const;
    const QVector<KeyRef>& selection() const { return m_selection; }
    bool hasKeyframeSelection() const { return !m_selection.isEmpty(); }

    // Rebuild the row layout from the model (structure change) and repaint. Cheap
    // enough for edits; NOT called during playback (see onFrameChanged()).
    void rebuild();

    int frameWidth() const { return m_frameWidth; }

    // Model mutations on the current selection, routed through the controller's
    // commands / undo-redo. Shared by the drag interaction and the toolbar.
    void moveSelection(int deltaFrames);   // one undo entry, snaps to whole frames
    void deleteSelection();                 // one undo entry

public slots:
    void setFrameWidth(int pixelsPerFrame);   // zoom
    void onFrameChanged();                     // playhead moved: repaint only

signals:
    void currentRowChanged();
    void selectionChanged();

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    struct Row {
        enum class Kind { LayerHeader, Property, Raster };
        Kind kind = Kind::Property;
        LayerId layer;
        anim::Property prop = anim::Property::Opacity;
        QString label;
        int top = 0;      // y within the scrollable content (ruler excluded)
        int height = 0;
        bool collapsible = false;   // LayerHeader: has property/raster rows to show
        bool collapsed = false;     // LayerHeader: those rows are currently hidden
    };

    struct Palette {
        QColor gutter, ruler, rowEven, rowOdd, header, divider, gridLine;
        QColor text, textDim, keyframe, keyframeSel, selectedRow, marker, boxFill;
    };

    Document* document() const;
    int startFrame() const;
    int endFrame() const;

    Palette colors() const;
    void rebuildRows();
    void pruneSelection();
    void updateScrollBars();

    int hscroll() const;
    int vscroll() const;
    qreal frameCenterX(int frame) const;
    int frameAtX(int x) const;
    int rowAtY(int y) const;             // index into m_rows, or -1
    int keyframeHalf(int rowHeight) const;

    int clampDelta(int rawDelta) const;

    bool selectionContains(const KeyRef& ref) const;
    void toggleInSelection(const KeyRef& ref);
    bool keyframeAt(const QPoint& pos, KeyRef& out) const;
    void recomputeBoxSelection();
    void setCurrentRowFromIndex(int rowIndex);
    void setCurrentRowFromKey(const KeyRef& key);
    void scrubTo(int viewportX);
    int rowCenterY(int rowIndex) const;
    QString propertyLabel(anim::Property p) const;

    ImageController* m_controller = nullptr;
    QVector<QMetaObject::Connection> m_connections;

    QVector<Row> m_rows;
    int m_contentHeight = 0;
    int m_frameWidth = 12;

    QSet<LayerId> m_collapsed;   // layers whose property/raster rows are hidden

    QVector<KeyRef> m_selection;
    RowRef m_currentRow;

    // Interaction state machine.
    enum class Drag { None, Scrub, MaybeKeys, Keys, MaybeBox, Box };
    Drag m_drag = Drag::None;
    QPoint m_pressPos;
    int m_pressFrame = 0;
    int m_dragAnchorFrame = 0;   // frame of the key under the press, for the badge
    int m_dragDelta = 0;         // clamped, live during a Keys drag
    KeyRef m_pressKey;           // the key hit on press (for click-to-collapse)
    bool m_pressModifier = false;
    QRect m_box;                 // viewport coords, live during a Box drag
    QVector<KeyRef> m_selectionBeforeBox;
    bool m_boxAdditive = false;
};
