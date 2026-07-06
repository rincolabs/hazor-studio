#include "ColorSwapWidget.hpp"

#include "core/ColorEngine.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "colorpicker/ColorPickerDialog.hpp"

#include <QPainter>
#include <QMouseEvent>
#include <QPainterPath>
#include <QStyleOption>

ColorSwapWidget::ColorSwapWidget(ColorEngine* colorEngine, QWidget* parent)
    : QWidget(parent)
    , m_colorEngine(colorEngine)
{
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setFixedHeight(58);
    updateRects();

    if (m_colorEngine) {
        m_fg = m_colorEngine->foregroundColor();
        m_bg = m_colorEngine->backgroundColor();

        connect(m_colorEngine, &ColorEngine::foregroundColorChanged, this, [this](const QColor& c) {
            m_fg = c;
            update();
        });
        connect(m_colorEngine, &ColorEngine::backgroundColorChanged, this, [this](const QColor& c) {
            m_bg = c;
            update();
        });
    }
}

void ColorSwapWidget::setActiveTargetMode(bool on)
{
    if (m_activeTargetMode == on) return;
    m_activeTargetMode = on;
    update();
}

void ColorSwapWidget::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    updateRects();
}

void ColorSwapWidget::updateRects()
{
    const int w = width();
    const int h = height();

    const int sq = qMin(24, h - 20);
    if (sq < 8) {
        m_fgRect = m_bgRect = m_swapRect = m_resetRect = QRect();
        return;
    }

    const int off = qMax(5, sq / 2);

    int cx = w / 2;
    int cy = h / 2 - 5;

    m_fgRect = QRect(cx - sq / 2 - off / 2, cy - sq / 2 - off / 2, sq, sq);
    m_bgRect = QRect(cx - sq / 2 + off / 2, cy - sq / 2 + off / 2, sq, sq);

    // Compact professional action pills below swatches
    int iconY = qMax(m_fgRect.bottom(), m_bgRect.bottom()) + 4;
    int pillW = 16;
    int pillH = 12;
    int gap = 4;
    int x0 = cx - pillW - gap / 2;

    m_swapRect = QRect(x0, iconY, pillW, pillH);
    m_resetRect = QRect(x0 + pillW + gap, iconY, pillW, pillH);
}

void ColorSwapWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    auto* t = ThemeManager::instance()->current();

    auto drawCheckerboard = [&](const QRect& rect) {
        const int cs = 4;
        for (int r = rect.top(); r < rect.bottom(); r += cs)
            for (int c = rect.left(); c < rect.right(); c += cs) {
                bool light = ((r / cs) + (c / cs)) % 2 == 0;
                p.fillRect(c, r, cs, cs, light ? QColor(190, 190, 190) : QColor(140, 140, 140));
            }
    };

    auto drawSwatch = [&](const QRect& rect, const QColor& color, bool hovered) {
        if (rect.isEmpty()) return;
        QRect inner = rect.adjusted(1, 1, -1, -1);

        if (color.alpha() < 255)
            drawCheckerboard(inner);

        if (color.alpha() > 0) {
            p.fillRect(inner, color);
        }

        QColor bc = hovered ? t->colorAccent : t->colorBorder;
        p.setPen(QPen(bc, hovered ? 2 : 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect);
    };

    auto drawSwapIcon = [&](const QRect& rect, bool hovered) {
        if (rect.isEmpty()) return;
        const QColor bg = hovered ? t->colorSurfaceHover : t->colorSurface;
        const QColor bd = hovered ? t->colorAccent : t->colorBorder;
        QColor ic = hovered ? t->colorAccent : t->colorTextSecondary;
        p.setBrush(bg);
        p.setPen(QPen(bd, 1));
        p.drawRoundedRect(rect, 3, 3);
        p.setPen(QPen(ic, 1.4));
        p.setBrush(Qt::NoBrush);

        int cx = rect.center().x();
        int cy = rect.center().y();
        int s = qMax(2, rect.width() / 3);

        p.drawLine(cx - s, cy - 2, cx + s - 1, cy - 2);
        p.drawLine(cx + s - 1, cy - 2, cx + s - 3, cy - 4);
        p.drawLine(cx + s - 1, cy - 2, cx + s - 3, cy + 0);

        p.drawLine(cx + s, cy + 2, cx - s + 1, cy + 2);
        p.drawLine(cx - s + 1, cy + 2, cx - s + 3, cy + 0);
        p.drawLine(cx - s + 1, cy + 2, cx - s + 3, cy + 4);
    };

    auto drawResetIcon = [&](const QRect& rect, bool hovered) {
        if (rect.isEmpty()) return;
        const QColor bg = hovered ? t->colorSurfaceHover : t->colorSurface;
        const QColor bd = hovered ? t->colorAccent : t->colorBorder;
        QColor ic = hovered ? t->colorAccent : t->colorTextSecondary;
        p.setBrush(bg);
        p.setPen(QPen(bd, 1));
        p.drawRoundedRect(rect, 3, 3);
        p.setPen(QPen(ic, 1.3));
        int cx = rect.center().x();
        int cy = rect.center().y();
        int r = qMax(2, rect.height() / 3);
        p.drawRect(cx - r, cy - r, r * 2, r * 2);
    };

    drawSwatch(m_bgRect, m_bg, m_hovered == Background);
    drawSwatch(m_fgRect, m_fg, m_hovered == Foreground);

    // Highlight the active edit target (Color panel mode only).
    if (m_activeTargetMode) {
        const QRect& activeRect = (m_activeTarget == Background) ? m_bgRect : m_fgRect;
        if (!activeRect.isEmpty()) {
            p.setPen(QPen(t->colorAccent, 2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(activeRect.adjusted(-2, -2, 2, 2));
        }
    }

    drawSwapIcon(m_swapRect, m_hovered == Swap);
    drawResetIcon(m_resetRect, m_hovered == Reset);
}

ColorSwapWidget::Area ColorSwapWidget::hitTest(const QPoint& pos) const
{
    // FG is drawn on top → checked first for hit priority
    if (m_fgRect.contains(pos)) return Foreground;
    if (m_bgRect.contains(pos)) return Background;
    if (m_swapRect.contains(pos)) return Swap;
    if (m_resetRect.contains(pos)) return Reset;
    return None;
}

void ColorSwapWidget::openPickerFor(Area area)
{
    if (area != Foreground && area != Background) return;

    QPointer<QDialog>& guard = (area == Foreground) ? m_fgPickerDlg : m_bgPickerDlg;
    if (guard) { guard->raise(); guard->activateWindow(); return; }

    QColor current = (area == Foreground) ? m_fg : m_bg;
    ColorPickerMode mode = (area == Foreground)
        ? ColorPickerMode::Foreground
        : ColorPickerMode::Background;

    auto* dlg = new ColorPickerDialog(current, mode, nullptr);
    guard = dlg;
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    if (area == Foreground) {
        connect(dlg, &ColorPickerDialog::colorAccepted, this, [this](const QColor& color) {
            m_colorEngine->setForegroundColor(color);
        });
    } else {
        connect(dlg, &ColorPickerDialog::colorAccepted, this, [this](const QColor& color) {
            m_colorEngine->setBackgroundColor(color);
        });
    }
    connect(dlg, &ColorPickerDialog::swatchAddRequested, this, [this](const QColor& color) {
        m_colorEngine->addSwatchColor(color);
    });

    dlg->open();
}

void ColorSwapWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);
        return;
    }

    Area area = hitTest(e->pos());
    if (area == None) return;

    switch (area) {
    case Foreground:
    case Background:
        if (m_activeTargetMode) {
            // Single click selects the active edit target; dialog via double-click.
            if (m_activeTarget != area) {
                m_activeTarget = area;
                update();
                emit activeTargetChanged(area == Foreground);
            }
        } else {
            openPickerFor(area);
        }
        break;
    case Swap:
        m_colorEngine->swapForegroundBackground();
        break;
    case Reset:
        m_colorEngine->resetDefaultColors();
        break;
    default:
        break;
    }
}

void ColorSwapWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || !m_activeTargetMode) {
        QWidget::mouseDoubleClickEvent(e);
        return;
    }

    Area area = hitTest(e->pos());
    if (area == Foreground || area == Background)
        openPickerFor(area);
}

void ColorSwapWidget::mouseMoveEvent(QMouseEvent* e)
{
    Area newHover = hitTest(e->pos());
    if (newHover != m_hovered) {
        m_hovered = newHover;
        switch (newHover) {
        case Foreground: setToolTip(tr("Foreground Color")); break;
        case Background: setToolTip(tr("Background Color")); break;
        case Swap:       setToolTip(tr("Swap Foreground/Background (X)")); break;
        case Reset:      setToolTip(tr("Reset Colors (D)")); break;
        default:         setToolTip(QString()); break;
        }
        update();
    }
}

void ColorSwapWidget::enterEvent(QEnterEvent*)
{
    update();
}

void ColorSwapWidget::leaveEvent(QEvent*)
{
    if (m_hovered != None) {
        m_hovered = None;
        update();
    }
}
