#pragma once

#include <QStyledItemDelegate>

class BrushPreviewCache;
struct BrushPreset;

// Draws one preset row: a tip-shape preview (column 1), a live stroke preview
// rendered by the real brush pipeline (column 2), and the preset name. All
// previews come from a shared BrushPreviewCache so painting never regenerates
// thumbnails. Selection and hover use theme surface colours.
class BrushPresetItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit BrushPresetItemDelegate(BrushPreviewCache* cache, QObject* parent = nullptr);

    // Compact mode draws only the centred tip thumbnail (for the horizontal dab
    // strip); the default full mode draws tip + stroke + name (the main list).
    void setCompact(bool compact) { m_compact = compact; }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;

    // Thumbnail sizes paint() will request — shared with the cache-warm job so
    // it pre-renders under the exact keys the lazy path looks up. The stroke is
    // rendered at a fixed reference size (independent of the row width) and
    // scaled into the row when painted, so it can be fully pre-warmed at startup
    // before the panel ever gets a real width.
    QSize tipThumbSize() const { return QSize(kTipSize, kTipSize); }
    QSize compactTipThumbSize() const { return QSize(kCompactTip, kCompactTip); }
    QSize strokeRefSize() const;

private:
    struct RowLayout { QRect tip; QRect stroke; QRect name; bool hasStroke = false; };
    // fillHeight makes the tip thumbnail as tall as the row (used for presets that
    // carry their own preview image), instead of the small fixed tip square.
    RowLayout layoutFor(const QRect& rect, bool fillHeight = false) const;

    void paintCompact(QPainter* painter, const QStyleOptionViewItem& option,
                      const BrushPreset& preset) const;

    BrushPreviewCache* m_cache = nullptr;
    bool m_compact = false;

    // Full-row height of the Brushes list. Kept equal to the Brush Settings
    // stroke preview height (StrokePreviewWidget min height, 78) so the two
    // previews read at the same scale. Only the full mode (the main list) uses
    // this; the compact strip/grid use kCompactCell.
    static constexpr int kRowHeight = 78;
    static constexpr int kTipSize = 40;
    static constexpr int kNameHeight = 16;
    static constexpr int kCompactCell = 46;
    static constexpr int kCompactTip = 36;
    // Fixed render width for stroke previews (≈ a default-width row's stroke
    // column); scaled to the actual cell when painted.
    static constexpr int kStrokeRefWidth = 240;
};
