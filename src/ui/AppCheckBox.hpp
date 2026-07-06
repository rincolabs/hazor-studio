#pragma once

#include <QCheckBox>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionButton>
#include <QMouseEvent>

#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

class AppCheckBox : public QCheckBox
{
public:
    struct Metrics
    {
        int horizontalPadding = 2;
        int verticalPadding = 2;
        int contentInset = 1;
        int indicatorSize = -1;
        int labelSpacing = -1;
        int checkInset = 5;
        int focusRingPadding = 1;
        qreal indicatorRadius = 2.0;
        qreal focusRadius = 3.0;
        qreal checkStrokeWidth = 1.6;
    };

    struct Colors
    {
        QColor boxFill;
        QColor boxFillHover;
        QColor boxFillPressed;
        QColor boxFillDisabled;
        QColor borderColor;
        QColor borderHoverColor;
        QColor borderFocusColor;
        QColor textColor;
        QColor textDisabledColor;
        QColor checkColor;
    };

    Metrics metrics;
    Colors colors;

    explicit AppCheckBox(QWidget* parent = nullptr)
        : QCheckBox(parent)
    {
        init();
    }

    explicit AppCheckBox(const QString& text, QWidget* parent = nullptr)
        : QCheckBox(text, parent)
    {
        init();
    }

    QSize sizeHint() const override
    {
        const QFont font = themedFont();
        const QFontMetrics metrics(font);
        const QString label = displayText();
        const int textWidth = label.isEmpty() ? 0 : metrics.horizontalAdvance(label);
        const int indicator = indicatorSize();
        const int spacing = label.isEmpty() ? 0 : labelSpacing();

        return QSize(
            indicator + spacing + textWidth + (this->metrics.horizontalPadding * 2),
            std::max(indicator, metrics.height()) + (this->metrics.verticalPadding * 2));
    }

    QSize minimumSizeHint() const override
    {
        return sizeHint();
    }

    void refreshStyle()
    {
        updateGeometry();
        update();
    }

    // When false, only a click on the indicator box toggles the check; a click on
    // the label area neither toggles nor is consumed (it is ignored so a parent
    // row wrapper can use it to select/activate the row instead). Default true
    // keeps the normal whole-widget toggle behaviour.
    void setLabelTogglesCheck(bool on) { m_labelTogglesCheck = on; }
    bool labelTogglesCheck() const { return m_labelTogglesCheck; }

protected:
    // Only the indicator box counts as the button when the label must not toggle.
    bool hitButton(const QPoint& pos) const override
    {
        if (m_labelTogglesCheck)
            return QCheckBox::hitButton(pos);
        return indicatorRect().contains(pos);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (!m_labelTogglesCheck && !indicatorRect().contains(event->pos())) {
            // Let the press bubble to the parent (row wrapper) for selection.
            event->ignore();
            return;
        }
        QCheckBox::mousePressEvent(event);
    }

    void paintEvent(QPaintEvent*) override
    {
        QStyleOptionButton option;
        initStyleOption(&option);

        const auto checkboxTheme = checkboxStyle();
        const int indicator = indicatorSize();
        const int spacing = text().isEmpty() ? 0 : labelSpacing();
        const bool rtl = layoutDirection() == Qt::RightToLeft;

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setFont(themedFont());

        const QRect contentRect = rect().adjusted(
            metrics.contentInset,
            metrics.contentInset,
            -metrics.contentInset,
            -metrics.contentInset);
        const int indicatorY = contentRect.top() + ((contentRect.height() - indicator) / 2);
        const int indicatorX = rtl ? contentRect.right() - indicator + 1 : contentRect.left();
        const QRect indicatorRect(indicatorX, indicatorY, indicator, indicator);

        const int textLeft = rtl ? contentRect.left() : (indicatorRect.right() + 1 + spacing);
        const int textRight = rtl ? (indicatorRect.left() - spacing - 1) : contentRect.right();
        QRect textRect;
        if (textRight >= textLeft)
            textRect = QRect(textLeft, 0, textRight - textLeft + 1, height());

        const bool enabled = option.state.testFlag(QStyle::State_Enabled);
        const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
        const bool focused = option.state.testFlag(QStyle::State_HasFocus);
        const bool sunken = option.state.testFlag(QStyle::State_Sunken);
        const bool checked = option.state.testFlag(QStyle::State_On);
        const bool partial = option.state.testFlag(QStyle::State_NoChange);

        QColor boxFill = resolvedColor(colors.boxFill, checkboxTheme.background, QColor("#20242A"));
        QColor borderColor = resolvedColor(colors.borderColor, checkboxTheme.border, QColor("#4A515B"));
        QColor textColor = resolvedColor(colors.textColor, checkboxTheme.text, QColor("#C7D0DB"));

        if (!enabled) {
            boxFill = resolvedColor(colors.boxFillDisabled, checkboxTheme.backgroundDisabled, boxFill.darker(108));
            borderColor = resolvedColor(colors.textDisabledColor, checkboxTheme.textDisabled, borderColor.darker(110));
            textColor = resolvedColor(colors.textDisabledColor, checkboxTheme.textDisabled, textColor.darker(130));
        } else if (sunken) {
            boxFill = resolvedColor(colors.boxFillPressed, checkboxTheme.backgroundPressed, boxFill.lighter(106));
        } else if (hovered) {
            boxFill = resolvedColor(colors.boxFillHover, checkboxTheme.backgroundHover, boxFill.lighter(110));
            borderColor = resolvedColor(colors.borderHoverColor, checkboxTheme.borderHover, borderColor.lighter(115));
        }

        if (focused && enabled) {
            painter.setPen(QPen(resolvedColor(colors.borderFocusColor, checkboxTheme.borderFocused, borderColor), 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(
                QRectF(indicatorRect).adjusted(
                    -metrics.focusRingPadding,
                    -metrics.focusRingPadding,
                    metrics.focusRingPadding,
                    metrics.focusRingPadding),
                metrics.focusRadius,
                metrics.focusRadius);
        }

        painter.setPen(QPen(borderColor, 1.0));
        painter.setBrush(boxFill);
        painter.drawRoundedRect(
            QRectF(indicatorRect).adjusted(0.5, 0.5, -0.5, -0.5),
            metrics.indicatorRadius,
            metrics.indicatorRadius);

        if (checked || partial) {
            painter.setPen(QPen(
                resolvedColor(colors.checkColor, checkboxTheme.text, QColor("#F4F6F8")),
                metrics.checkStrokeWidth,
                Qt::SolidLine,
                Qt::RoundCap,
                Qt::RoundJoin));
            if (partial) {
                const int midY = indicatorRect.center().y();
                painter.drawLine(
                    indicatorRect.left() + metrics.checkInset,
                    midY,
                    indicatorRect.right() - metrics.checkInset,
                    midY);
            } else {
                const qreal inset = static_cast<qreal>(metrics.checkInset);
                const qreal left = static_cast<qreal>(indicatorRect.left());
                const qreal right = static_cast<qreal>(indicatorRect.right());
                const qreal top = static_cast<qreal>(indicatorRect.top());
                const qreal bottom = static_cast<qreal>(indicatorRect.bottom());
                QPainterPath mark;
                mark.moveTo(left + inset, indicatorRect.center().y() + 0.5);
                mark.lineTo(left + inset + 2.5, bottom - inset + 0.5);
                mark.lineTo(right - inset, top + inset);
                painter.drawPath(mark);
            }
        }

        if (!textRect.isNull() && !text().isEmpty()) {
            QPalette pal = palette();
            pal.setColor(QPalette::WindowText, textColor);
            const int flags = Qt::AlignVCenter | (rtl ? Qt::AlignRight : Qt::AlignLeft) | Qt::TextShowMnemonic;
            style()->drawItemText(&painter, textRect, flags, pal, enabled, text(), QPalette::WindowText);
        }
    }

private:
    void init()
    {
        setAttribute(Qt::WA_Hover, true);
        setAutoFillBackground(false);
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

        QObject::connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
            updateGeometry();
            update();
        });
    }

    static QColor colorOrFallback(const QColor& color, const QColor& fallback)
    {
        return color.isValid() ? color : fallback;
    }

    static QColor resolvedColor(const QColor& overrideColor, const QColor& themedColor, const QColor& fallback)
    {
        if (overrideColor.isValid())
            return overrideColor;
        return colorOrFallback(themedColor, fallback);
    }

    QString displayText() const
    {
        QString label = text();
        label.replace(QStringLiteral("&&"), QChar(0x01));
        label.remove(QLatin1Char('&'));
        label.replace(QChar(0x01), QLatin1String("&"));
        return label;
    }

    QFont themedFont() const
    {
        QFont result = font();
        const auto style = checkboxStyle();
        if (style.fontSize > 0)
            result.setPixelSize(style.fontSize);
        if (style.fontWeight > 0)
            result.setWeight(static_cast<QFont::Weight>(style.fontWeight));
        if (!style.fontFamily.isEmpty())
            result.setFamily(style.fontFamily);
        return result;
    }

    QtComponentStyle checkboxStyle() const
    {
        if (auto* theme = ThemeManager::instance()->current())
            return theme->componentTheme().QCheckBox;
        return {};
    }

    int indicatorSize() const
    {
        if (metrics.indicatorSize > 0)
            return metrics.indicatorSize;
        const auto style = checkboxStyle();
        return style.minWidth > 0 ? style.minWidth : 17;
    }

    int labelSpacing() const
    {
        if (metrics.labelSpacing >= 0)
            return metrics.labelSpacing;
        const auto style = checkboxStyle();
        return style.paddingLeft >= 0 ? style.paddingLeft : 4;
    }

    // The indicator box rect, matching the geometry paintEvent() draws it at.
    QRect indicatorRect() const
    {
        const QRect contentRect = rect().adjusted(
            metrics.contentInset, metrics.contentInset,
            -metrics.contentInset, -metrics.contentInset);
        const int indicator = indicatorSize();
        const int indicatorY = contentRect.top() + ((contentRect.height() - indicator) / 2);
        const bool rtl = layoutDirection() == Qt::RightToLeft;
        const int indicatorX = rtl ? contentRect.right() - indicator + 1 : contentRect.left();
        return QRect(indicatorX, indicatorY, indicator, indicator);
    }

    bool m_labelTogglesCheck = true;
};
