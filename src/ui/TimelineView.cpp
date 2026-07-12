#include "TimelineView.hpp"

#include "controller/ImageController.hpp"
#include "controller/CommandHistory.hpp"
#include "core/Document.hpp"
#include "core/LayerTreeNode.hpp"
#include "animation/AnimationModel.hpp"
#include "animation/AnimationTrack.hpp"
#include "animation/AnimationCommands.hpp"
#include "animation/PlaybackController.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QScrollBar>

#include <algorithm>
#include <climits>
#include <cmath>

namespace {
constexpr int kLabelWidth = 190;
constexpr int kRulerHeight = 22;
constexpr int kLayerHeaderHeight = 22;
constexpr int kRowHeight = 18;
constexpr int kMinLabelPx = 36;   // min pixels between ruler frame numbers

// Round a raw step up to a "nice" value (1,2,5 × 10ⁿ) so ruler labels land on
// readable frame numbers regardless of zoom.
int niceStep(int raw)
{
    if (raw <= 1) return 1;
    int mag = 1;
    while (mag * 10 < raw) mag *= 10;
    for (int b : {1, 2, 5}) {
        const int step = b * mag;
        if (step >= raw) return step;
    }
    return mag * 10;
}
}  // namespace

TimelineView::TimelineView(QWidget* parent)
    : QAbstractScrollArea(parent)
{
    setObjectName(QStringLiteral("TimelineView"));
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setMouseTracking(true);
    horizontalScrollBar()->setSingleStep(m_frameWidth);
    verticalScrollBar()->setSingleStep(kRowHeight);

    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, [this] { viewport()->update(); });
}

// ── Controller wiring ───────────────────────────────────────────────────────
void TimelineView::setController(ImageController* controller)
{
    for (const auto& c : m_connections)
        disconnect(c);
    m_connections.clear();
    m_controller = controller;
    m_selection.clear();
    m_currentRow = {};
    m_collapsed.clear();
    if (m_controller) {
        // Playback / manual frame change only moves the marker — no rebuild. This
        // is the one signal the view watches directly because it is high-frequency
        // during playback; structural rebuilds are driven by TimelinePanel.
        m_connections.push_back(connect(m_controller, &ImageController::currentFrameChanged,
            this, &TimelineView::onFrameChanged));
    }
    rebuild();
}

Document* TimelineView::document() const
{
    return m_controller ? m_controller->document() : nullptr;
}

int TimelineView::startFrame() const
{
    Document* doc = document();
    return doc ? doc->animation.startFrame() : 0;
}

int TimelineView::endFrame() const
{
    Document* doc = document();
    return doc ? std::max(doc->animation.startFrame(), doc->animation.endFrame()) : 0;
}

// ── Theme ───────────────────────────────────────────────────────────────────
TimelineView::Palette TimelineView::colors() const
{
    Palette p;
    Theme* t = ThemeManager::instance()->current();
    if (!t) {
        // Neutral fallback so the widget is still legible before a theme loads.
        p.gutter = QColor("#20242A"); p.ruler = QColor("#1A1D22");
        p.rowEven = QColor("#22262C"); p.rowOdd = QColor("#25292F");
        p.header = QColor("#1C2026"); p.divider = QColor("#12151A");
        p.gridLine = QColor("#2E333B"); p.text = QColor("#E6E6E6");
        p.textDim = QColor("#9AA3AE"); p.keyframe = QColor("#9AA3AE");
        p.keyframeSel = QColor("#5C8FD8"); p.selectedRow = QColor("#33465E");
        p.marker = QColor("#5C8FD8"); p.boxFill = QColor("#5C8FD8");
        return p;
    }
    p.gutter = t->colorPanelBackground;
    p.ruler = t->colorBackgroundTertiary;
    p.rowEven = t->colorSurface;
    p.rowOdd = t->colorSurfaceRow;
    p.header = t->colorBackgroundSecondary;
    p.divider = t->colorBorder;
    p.gridLine = t->colorBorderLight;
    p.text = t->colorTextPrimary;
    p.textDim = t->colorTextSecondary;
    p.keyframe = t->colorTextSecondary;
    p.keyframeSel = t->colorAccent;
    p.selectedRow = t->colorSurfaceSelected;
    p.marker = t->colorAccent;
    p.boxFill = t->colorAccent;
    return p;
}

// ── Row layout ──────────────────────────────────────────────────────────────
QString TimelineView::propertyLabel(anim::Property p) const
{
    switch (p) {
    case anim::Property::PositionX: return tr("Position X");
    case anim::Property::PositionY: return tr("Position Y");
    case anim::Property::ScaleX:    return tr("Scale X");
    case anim::Property::ScaleY:    return tr("Scale Y");
    case anim::Property::Rotation:  return tr("Rotation");
    case anim::Property::SkewX:     return tr("Skew X");
    case anim::Property::SkewY:     return tr("Skew Y");
    case anim::Property::PivotX:    return tr("Pivot X");
    case anim::Property::PivotY:    return tr("Pivot Y");
    case anim::Property::Opacity:   return tr("Opacity");
    case anim::Property::Visibility:return tr("Visibility");
    case anim::Property::BlendMode: return tr("Blend Mode");
    }
    return {};
}

void TimelineView::rebuildRows()
{
    m_rows.clear();
    m_contentHeight = 0;
    Document* doc = document();
    if (!doc) return;

    int y = 0;
    for (LayerTreeNode* node : doc->flatten()) {
        if (!node || node->type != LayerTreeNode::Type::Layer) continue;
        // Every layer always exposes all of its animatable properties, whether or
        // not it is selected and whether or not they already have keyframes. The
        // header is therefore always collapsible.
        const bool collapsed = m_collapsed.contains(node->id);

        Row header;
        header.kind = Row::Kind::LayerHeader;
        header.layer = node->id;
        header.label = node->name;
        header.top = y;
        header.height = kLayerHeaderHeight;
        header.collapsible = true;
        header.collapsed = collapsed;
        m_rows.push_back(header);
        y += kLayerHeaderHeight;

        if (collapsed) continue;   // header only; hide the animated property rows

        for (anim::Property prop : anim::allProperties()) {
            Row r;
            r.kind = Row::Kind::Property;
            r.layer = node->id;
            r.prop = prop;
            r.label = propertyLabel(prop);
            r.top = y;
            r.height = kRowHeight;
            m_rows.push_back(r);
            y += kRowHeight;
        }
        if (doc->animation.rasterTrack(node->id)) {
            Row r;
            r.kind = Row::Kind::Raster;
            r.layer = node->id;
            r.label = tr("Raster Cels");
            r.top = y;
            r.height = kRowHeight;
            m_rows.push_back(r);
            y += kRowHeight;
        }
    }
    m_contentHeight = y;
}

void TimelineView::pruneSelection()
{
    Document* doc = document();
    if (!doc) { m_selection.clear(); return; }
    QVector<KeyRef> kept;
    kept.reserve(m_selection.size());
    for (const KeyRef& ref : m_selection) {
        const anim::AnimationTrack* t = doc->animation.track(ref.layer, ref.prop);
        if (t && t->hasKeyframeAt(ref.frame))
            kept.push_back(ref);
    }
    m_selection = kept;
}

void TimelineView::rebuild()
{
    rebuildRows();
    pruneSelection();
    updateScrollBars();
    viewport()->update();
}

void TimelineView::onFrameChanged()
{
    // Playhead moved: no structural change, just repaint the marker.
    viewport()->update();
}

// ── Scroll geometry ─────────────────────────────────────────────────────────
void TimelineView::updateScrollBars()
{
    Document* doc = document();
    const int frames = doc ? std::max(1, endFrame() - startFrame() + 1) : 1;
    const int contentW = frames * m_frameWidth;
    const int viewW = std::max(0, viewport()->width() - kLabelWidth);
    horizontalScrollBar()->setRange(0, std::max(0, contentW - viewW));
    horizontalScrollBar()->setPageStep(std::max(1, viewW));
    horizontalScrollBar()->setSingleStep(m_frameWidth);

    const int viewH = std::max(0, viewport()->height() - kRulerHeight);
    verticalScrollBar()->setRange(0, std::max(0, m_contentHeight - viewH));
    verticalScrollBar()->setPageStep(std::max(1, viewH));
    verticalScrollBar()->setSingleStep(kRowHeight);
}

void TimelineView::resizeEvent(QResizeEvent*)
{
    updateScrollBars();
    viewport()->update();
}

void TimelineView::scrollContentsBy(int, int)
{
    // Frozen ruler/gutter make a scroll-blit unsafe; repaint everything instead.
    viewport()->update();
}

int TimelineView::hscroll() const { return horizontalScrollBar()->value(); }
int TimelineView::vscroll() const { return verticalScrollBar()->value(); }

qreal TimelineView::frameCenterX(int frame) const
{
    return kLabelWidth + (frame - startFrame() + 0.5) * m_frameWidth - hscroll();
}

int TimelineView::frameAtX(int x) const
{
    return startFrame() +
        static_cast<int>(std::floor((x - kLabelWidth + hscroll()) / double(m_frameWidth)));
}

int TimelineView::rowAtY(int y) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        const int vy = kRulerHeight + m_rows[i].top - vscroll();
        if (y >= vy && y < vy + m_rows[i].height) return i;
    }
    return -1;
}

int TimelineView::rowCenterY(int rowIndex) const
{
    const Row& r = m_rows[rowIndex];
    return kRulerHeight + r.top - vscroll() + r.height / 2;
}

int TimelineView::keyframeHalf(int rowHeight) const
{
    return std::max(3, std::min(m_frameWidth, rowHeight) / 2 - 2);
}

// ── Zoom ────────────────────────────────────────────────────────────────────
void TimelineView::setFrameWidth(int pixelsPerFrame)
{
    const int fw = std::clamp(pixelsPerFrame, 4, 48);
    if (fw == m_frameWidth) return;
    m_frameWidth = fw;
    updateScrollBars();
    viewport()->update();
}

// ── Painting ────────────────────────────────────────────────────────────────
void TimelineView::paintEvent(QPaintEvent*)
{
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing, true);
    const Palette pal = colors();
    const int w = viewport()->width();
    const int h = viewport()->height();

    p.fillRect(0, 0, w, h, pal.rowEven);

    Document* doc = document();
    if (!doc) {
        p.setPen(pal.textDim);
        p.drawText(viewport()->rect(), Qt::AlignCenter, tr("No document"));
        return;
    }

    const int start = startFrame();
    const int end = endFrame();
    const int firstVisible = std::max(start, frameAtX(kLabelWidth));
    const int lastVisible = std::min(end, frameAtX(w) + 1);

    const int rawStep = std::max(1, static_cast<int>(std::ceil(kMinLabelPx / double(m_frameWidth))));
    const int labelStep = niceStep(rawStep);

    // ── Frame area: rows, grid, keyframes (clipped so it can't paint over the
    //    frozen ruler/gutter) ──
    p.save();
    p.setClipRect(kLabelWidth, kRulerHeight, w - kLabelWidth, h - kRulerHeight);

    for (int i = 0; i < m_rows.size(); ++i) {
        const Row& r = m_rows[i];
        const int vy = kRulerHeight + r.top - vscroll();
        if (vy + r.height < kRulerHeight || vy > h) continue;
        QColor bg = (r.kind == Row::Kind::LayerHeader) ? pal.header
                    : ((i & 1) ? pal.rowOdd : pal.rowEven);
        p.fillRect(kLabelWidth, vy, w - kLabelWidth, r.height, bg);
        p.setPen(pal.divider);
        p.drawLine(kLabelWidth, vy + r.height - 1, w, vy + r.height - 1);
    }

    // Vertical grid at labelled frames.
    QColor grid = pal.gridLine;
    grid.setAlpha(60);
    p.setPen(grid);
    for (int f = firstVisible; f <= lastVisible; ++f) {
        if ((f - start) % labelStep != 0) continue;
        const qreal x = frameCenterX(f);
        p.drawLine(QPointF(x, kRulerHeight), QPointF(x, h));
    }

    // Keyframes / cels.
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row& r = m_rows[i];
        const int vy = kRulerHeight + r.top - vscroll();
        if (vy + r.height < kRulerHeight || vy > h) continue;
        const int cy = vy + r.height / 2;
        const int half = keyframeHalf(r.height);

        if (r.kind == Row::Kind::Property) {
            const anim::AnimationTrack* t = doc->animation.track(r.layer, r.prop);
            if (!t) continue;
            for (const anim::Keyframe& k : t->keyframes()) {
                const bool sel = selectionContains({r.layer, r.prop, k.frame});
                int drawFrame = k.frame;
                bool ghost = false;
                if (m_drag == Drag::Keys && sel) { drawFrame = k.frame + m_dragDelta; ghost = true; }
                const qreal cx = frameCenterX(drawFrame);
                if (cx < kLabelWidth - half || cx > w + half) continue;
                QPolygonF diamond;
                diamond << QPointF(cx, cy - half) << QPointF(cx + half, cy)
                        << QPointF(cx, cy + half) << QPointF(cx - half, cy);
                QColor fill = sel ? pal.keyframeSel : pal.keyframe;
                if (ghost) fill.setAlpha(180);
                p.setPen(QPen(pal.divider, 1.0));
                p.setBrush(fill);
                p.drawPolygon(diamond);
            }
        } else if (r.kind == Row::Kind::Raster) {
            const anim::RasterCelTrack* rt = doc->animation.rasterTrack(r.layer);
            if (!rt) continue;
            for (const auto& [frame, celId] : rt->keyframes()) {
                const qreal cx = frameCenterX(frame);
                if (cx < kLabelWidth - half || cx > w + half) continue;
                const QRectF sq(cx - half, cy - half, half * 2, half * 2);
                p.setPen(QPen(pal.divider, 1.0));
                p.setBrush(celId ? pal.keyframe : Qt::NoBrush);
                p.drawRoundedRect(sq, 2, 2);
            }
        }
    }

    // Drag badge: destination frame of the anchored key.
    if (m_drag == Drag::Keys && m_dragDelta != 0) {
        const int target = m_dragAnchorFrame + m_dragDelta;
        const QString text = QString::number(target);
        QFont f = font(); f.setBold(true);
        p.setFont(f);
        const QFontMetrics fm(f);
        const int tw = fm.horizontalAdvance(text) + 10;
        const qreal bx = frameCenterX(target);
        QRectF badge(bx - tw / 2.0, kRulerHeight + 2, tw, fm.height() + 2);
        p.setPen(Qt::NoPen);
        p.setBrush(pal.keyframeSel);
        p.drawRoundedRect(badge, 3, 3);
        p.setPen(QColor(Qt::white));
        p.drawText(badge, Qt::AlignCenter, text);
    }
    p.restore();

    // ── Ruler (frozen top) ──
    p.fillRect(0, 0, w, kRulerHeight, pal.ruler);
    p.save();
    p.setClipRect(kLabelWidth, 0, w - kLabelWidth, kRulerHeight);
    p.setPen(pal.textDim);
    for (int f = firstVisible; f <= lastVisible; ++f) {
        const qreal x = frameCenterX(f);
        const bool labelled = (f - start) % labelStep == 0;
        p.setPen(pal.divider);
        p.drawLine(QPointF(x, labelled ? kRulerHeight - 8 : kRulerHeight - 4),
                   QPointF(x, kRulerHeight));
        if (labelled) {
            p.setPen(pal.textDim);
            p.drawText(QRectF(x - m_frameWidth * labelStep / 2.0, 0,
                              m_frameWidth * labelStep, kRulerHeight - 8),
                       Qt::AlignCenter, QString::number(f));
        }
    }
    p.restore();
    p.setPen(pal.divider);
    p.drawLine(0, kRulerHeight - 1, w, kRulerHeight - 1);

    // ── Label gutter (frozen left) ──
    p.fillRect(0, kRulerHeight, kLabelWidth, h - kRulerHeight, pal.gutter);
    p.save();
    p.setClipRect(0, kRulerHeight, kLabelWidth, h - kRulerHeight);
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row& r = m_rows[i];
        const int vy = kRulerHeight + r.top - vscroll();
        if (vy + r.height < kRulerHeight || vy > h) continue;
        const bool isCurrent = m_currentRow.valid
            && ((r.kind == Row::Kind::Property && !m_currentRow.raster
                 && r.layer == m_currentRow.layer && r.prop == m_currentRow.prop)
             || (r.kind == Row::Kind::Raster && m_currentRow.raster
                 && r.layer == m_currentRow.layer));
        if (r.kind == Row::Kind::LayerHeader)
            p.fillRect(0, vy, kLabelWidth, r.height, pal.header);
        else if (isCurrent)
            p.fillRect(0, vy, kLabelWidth, r.height, pal.selectedRow);
        p.setPen(pal.divider);
        p.drawLine(0, vy + r.height - 1, kLabelWidth, vy + r.height - 1);

        QFont f = font();
        int indent = 20;
        if (r.kind == Row::Kind::LayerHeader) {
            f.setBold(true);
            indent = 20;
            // Disclosure triangle for collapsible layers.
            if (r.collapsible) {
                const int cy = vy + r.height / 2;
                p.setPen(Qt::NoPen);
                p.setBrush(pal.textDim);
                QPolygonF tri;
                if (r.collapsed)   // ▸ pointing right
                    tri << QPointF(7, cy - 4) << QPointF(12, cy) << QPointF(7, cy + 4);
                else               // ▾ pointing down
                    tri << QPointF(6, cy - 3) << QPointF(14, cy - 3) << QPointF(10, cy + 3);
                p.drawPolygon(tri);
            }
        }
        p.setFont(f);
        p.setPen(r.kind == Row::Kind::LayerHeader ? pal.text : pal.textDim);
        p.drawText(QRect(indent, vy, kLabelWidth - indent - 4, r.height),
                   Qt::AlignVCenter | Qt::AlignLeft, r.label);
    }
    p.restore();

    // Corner + gutter border.
    p.fillRect(0, 0, kLabelWidth, kRulerHeight, pal.ruler);
    p.setPen(pal.divider);
    p.drawLine(kLabelWidth, 0, kLabelWidth, h);
    p.drawLine(0, kRulerHeight - 1, kLabelWidth, kRulerHeight - 1);

    // ── Current-frame marker (drawn last, above rows/keyframes, clipped to the
    //    frame area so it never crosses the gutter and never blocks a click) ──
    const int current = doc->currentFrame();
    if (current >= start && current <= end) {
        const qreal mx = frameCenterX(current);
        if (mx >= kLabelWidth) {
            p.save();
            p.setClipRect(kLabelWidth, 0, w - kLabelWidth, h);
            p.setPen(QPen(pal.marker, 2.0));
            p.drawLine(QPointF(mx, 0), QPointF(mx, h));
            // Small handle in the ruler.
            p.setPen(Qt::NoPen);
            p.setBrush(pal.marker);
            QPolygonF head;
            head << QPointF(mx - 5, 0) << QPointF(mx + 5, 0) << QPointF(mx, 8);
            p.drawPolygon(head);
            p.restore();
        }
    }

    // ── Box selection rectangle ──
    if (m_drag == Drag::Box && !m_box.isNull()) {
        p.save();
        p.setClipRect(kLabelWidth, kRulerHeight, w - kLabelWidth, h - kRulerHeight);
        QColor fill = pal.boxFill; fill.setAlpha(48);
        p.setBrush(fill);
        p.setPen(QPen(pal.boxFill, 1.0));
        p.drawRect(m_box.normalized());
        p.restore();
    }
}

// ── Hit-testing ─────────────────────────────────────────────────────────────
bool TimelineView::keyframeAt(const QPoint& pos, KeyRef& out) const
{
    Document* doc = document();
    const int ri = rowAtY(pos.y());
    if (!doc || ri < 0) return false;
    const Row& r = m_rows[ri];
    if (r.kind != Row::Kind::Property) return false;
    const anim::AnimationTrack* t = doc->animation.track(r.layer, r.prop);
    if (!t) return false;
    const int cy = rowCenterY(ri);
    const int half = keyframeHalf(r.height) + 2;
    for (const anim::Keyframe& k : t->keyframes()) {
        const qreal cx = frameCenterX(k.frame);
        if (std::abs(pos.x() - cx) <= half && std::abs(pos.y() - cy) <= half) {
            out = {r.layer, r.prop, k.frame};
            return true;
        }
    }
    return false;
}

bool TimelineView::selectionContains(const KeyRef& ref) const
{
    return m_selection.contains(ref);
}

void TimelineView::toggleInSelection(const KeyRef& ref)
{
    const int idx = m_selection.indexOf(ref);
    if (idx >= 0) m_selection.remove(idx);
    else m_selection.push_back(ref);
}

TimelineView::RowRef TimelineView::currentRow() const
{
    return m_currentRow;
}

void TimelineView::setCurrentRowFromIndex(int rowIndex)
{
    RowRef row;
    if (rowIndex >= 0 && rowIndex < m_rows.size()) {
        const Row& r = m_rows[rowIndex];
        if (r.kind == Row::Kind::Property) {
            row = {true, false, r.layer, r.prop};
        } else if (r.kind == Row::Kind::Raster) {
            row = {true, true, r.layer, anim::Property::Opacity};
        }
    }
    m_currentRow = row;
    emit currentRowChanged();
}

void TimelineView::setCurrentRowFromKey(const KeyRef& key)
{
    m_currentRow = {true, false, key.layer, key.prop};
    emit currentRowChanged();
}

// ── Mouse ───────────────────────────────────────────────────────────────────
void TimelineView::scrubTo(int viewportX)
{
    if (!m_controller) return;
    const int frame = std::clamp(frameAtX(viewportX), startFrame(), endFrame());
    m_controller->playbackController()->goToFrame(frame);
}

void TimelineView::mousePressEvent(QMouseEvent* e)
{
    if (!document()) return;
    m_pressPos = e->pos();
    const int x = e->pos().x();
    const int y = e->pos().y();
    const bool modifier = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
    setFocus();

    // Ruler → scrub the playhead.
    if (y < kRulerHeight && x >= kLabelWidth) {
        m_drag = Drag::Scrub;
        scrubTo(x);
        return;
    }
    // Label gutter → toggle a layer header's collapse, else focus a row for the
    // toolbar single-key actions.
    if (x < kLabelWidth && y >= kRulerHeight) {
        const int ri = rowAtY(y);
        if (ri >= 0 && m_rows[ri].kind == Row::Kind::LayerHeader) {
            if (m_rows[ri].collapsible) {
                const LayerId id = m_rows[ri].layer;
                if (m_collapsed.contains(id)) m_collapsed.remove(id);
                else m_collapsed.insert(id);
                rebuild();
            }
        } else {
            setCurrentRowFromIndex(ri);
        }
        m_drag = Drag::None;
        viewport()->update();
        return;
    }
    if (x < kLabelWidth || y < kRulerHeight) return;   // corner

    // Frame area.
    KeyRef hit;
    if (keyframeAt(e->pos(), hit)) {
        setCurrentRowFromKey(hit);
        m_pressKey = hit;
        m_pressModifier = modifier;
        if (modifier) {
            toggleInSelection(hit);
        } else if (!selectionContains(hit)) {
            m_selection = {hit};
        }
        emit selectionChanged();
        m_dragAnchorFrame = hit.frame;
        m_pressFrame = frameAtX(x);
        m_dragDelta = 0;
        m_drag = Drag::MaybeKeys;
        viewport()->update();
        return;
    }

    // Empty area → focus the row, arm a box selection.
    setCurrentRowFromIndex(rowAtY(y));
    m_selectionBeforeBox = m_selection;
    m_boxAdditive = modifier;
    m_box = QRect(e->pos(), e->pos());
    m_drag = Drag::MaybeBox;
    viewport()->update();
}

void TimelineView::mouseMoveEvent(QMouseEvent* e)
{
    const int x = e->pos().x();
    const int threshold = QApplication::startDragDistance();

    if (m_drag == Drag::Scrub) { scrubTo(x); return; }

    if (m_drag == Drag::MaybeKeys) {
        if ((e->pos() - m_pressPos).manhattanLength() <= threshold) return;
        m_drag = Drag::Keys;
    }
    if (m_drag == Drag::Keys) {
        m_dragDelta = clampDelta(frameAtX(x) - m_pressFrame);
        viewport()->update();
        return;
    }
    if (m_drag == Drag::MaybeBox) {
        if ((e->pos() - m_pressPos).manhattanLength() <= threshold) return;
        m_drag = Drag::Box;
    }
    if (m_drag == Drag::Box) {
        m_box = QRect(m_pressPos, e->pos()).normalized();
        recomputeBoxSelection();
        viewport()->update();
    }
}

void TimelineView::mouseReleaseEvent(QMouseEvent*)
{
    switch (m_drag) {
    case Drag::Keys:
        if (m_dragDelta != 0) moveSelection(m_dragDelta);
        break;
    case Drag::MaybeKeys:
        // A plain click on an already-multi-selected key collapses to just it.
        if (!m_pressModifier) { m_selection = {m_pressKey}; emit selectionChanged(); }
        break;
    case Drag::Box:
        emit selectionChanged();
        break;
    case Drag::MaybeBox:
        // A plain click on empty space clears the selection.
        if (!m_boxAdditive && !m_selection.isEmpty()) {
            m_selection.clear();
            emit selectionChanged();
        }
        break;
    default:
        break;
    }
    m_drag = Drag::None;
    m_dragDelta = 0;
    m_box = QRect();
    viewport()->update();
}

void TimelineView::keyPressEvent(QKeyEvent* e)
{
    if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace)
        && !m_selection.isEmpty()) {
        deleteSelection();
        return;
    }
    QAbstractScrollArea::keyPressEvent(e);
}

void TimelineView::recomputeBoxSelection()
{
    Document* doc = document();
    if (!doc) return;
    const QRect box = m_box.normalized();
    QVector<KeyRef> hits;
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row& r = m_rows[i];
        if (r.kind != Row::Kind::Property) continue;
        const int vy = kRulerHeight + r.top - vscroll();
        if (vy + r.height < kRulerHeight || vy > viewport()->height()) continue;
        const anim::AnimationTrack* t = doc->animation.track(r.layer, r.prop);
        if (!t) continue;
        const int cy = vy + r.height / 2;
        const int half = keyframeHalf(r.height);
        for (const anim::Keyframe& k : t->keyframes()) {
            const qreal cx = frameCenterX(k.frame);
            const QRect kfRect(std::lround(cx) - half, cy - half, half * 2, half * 2);
            if (box.intersects(kfRect)) hits.push_back({r.layer, r.prop, k.frame});
        }
    }
    if (m_boxAdditive) {
        m_selection = m_selectionBeforeBox;
        for (const KeyRef& h : hits)
            if (!m_selection.contains(h)) m_selection.push_back(h);
    } else {
        m_selection = hits;
    }
}

// ── Selection mutations (through the command / undo-redo system) ─────────────
int TimelineView::clampDelta(int rawDelta) const
{
    if (m_selection.isEmpty()) return 0;
    int mn = INT_MAX, mx = INT_MIN;
    for (const KeyRef& ref : m_selection) {
        mn = std::min(mn, ref.frame);
        mx = std::max(mx, ref.frame);
    }
    const int lo = startFrame() - mn;   // most-negative delta keeping min >= start
    const int hi = endFrame() - mx;     // most-positive delta keeping max <= end
    if (lo > hi) return 0;              // selection span wider than the range
    return std::clamp(rawDelta, lo, hi);
}

void TimelineView::moveSelection(int rawDeltaFrames)
{
    Document* doc = document();
    if (!doc || m_selection.isEmpty()) return;
    const int delta = clampDelta(rawDeltaFrames);
    if (delta == 0) return;

    // Group the selection by (layer, property) so each track can move all of its
    // selected keys atomically (remove-all-then-reinsert), which keeps relative
    // spacing and lets a key overwrite an unselected one it lands on.
    struct Group { LayerId layer; anim::Property prop; QVector<int> frames; };
    QVector<Group> groups;
    for (const KeyRef& ref : m_selection) {
        Group* g = nullptr;
        for (Group& cand : groups)
            if (cand.layer == ref.layer && cand.prop == ref.prop) { g = &cand; break; }
        if (!g) { groups.push_back({ref.layer, ref.prop, {}}); g = &groups.back(); }
        g->frames.push_back(ref.frame);
    }

    anim::AnimationModel before = doc->animation;   // deep copy for undo
    anim::AnimationModel after = doc->animation;     // mutate this copy
    for (const Group& g : groups) {
        anim::AnimationTrack* t = after.track(g.layer, g.prop);
        if (!t) continue;
        QVector<anim::Keyframe> moved;
        for (int f : g.frames)
            if (const anim::Keyframe* k = t->keyframeAt(f)) moved.push_back(*k);
        for (int f : g.frames)
            t->removeKeyframe(f);
        for (anim::Keyframe kf : moved) {
            kf.frame += delta;
            t->setKeyframe(kf);
        }
    }

    auto cmd = std::make_unique<anim::AnimationModelStateCommand>(
        doc, std::move(before), std::move(after), tr("Move Keyframes"));
    cmd->execute();
    // Update the selection to the new frames BEFORE push(): push() can emit
    // historyChanged synchronously, which drives a rebuild() whose pruneSelection
    // would otherwise drop our (now stale) references.
    for (KeyRef& ref : m_selection)
        ref.frame += delta;
    m_controller->history().push(std::move(cmd));
    rebuildRows();
    updateScrollBars();
    emit selectionChanged();
    viewport()->update();
}

void TimelineView::deleteSelection()
{
    Document* doc = document();
    if (!doc || m_selection.isEmpty()) return;
    m_controller->history().beginMacro(tr("Delete Keyframes"));
    for (const KeyRef& ref : m_selection) {
        auto cmd = std::make_unique<anim::RemoveKeyframeCommand>(
            doc, ref.layer, ref.prop, ref.frame);
        cmd->execute();
        m_controller->history().push(std::move(cmd));
    }
    m_controller->history().endMacro();
    m_selection.clear();
    rebuild();
    emit selectionChanged();
}
