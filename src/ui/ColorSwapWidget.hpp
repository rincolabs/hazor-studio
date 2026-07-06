#pragma once

#include <QWidget>
#include <QColor>
#include <QPointer>
#include <QDialog>

class ColorEngine;

class ColorSwapWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QColor foregroundColor READ foregroundColor)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor)
    Q_PROPERTY(QPoint fgCenter READ fgCenter)
    Q_PROPERTY(QPoint bgCenter READ bgCenter)
    Q_PROPERTY(QPoint swapCenter READ swapCenter)
    Q_PROPERTY(QPoint resetCenter READ resetCenter)
    Q_PROPERTY(int hoveredArea READ hoveredArea)

public:
    explicit ColorSwapWidget(ColorEngine* colorEngine, QWidget* parent = nullptr);

    // When enabled, a single click on the FG/BG square selects it as the active
    // edit target (emitting activeTargetChanged) instead of opening the picker
    // dialog; the dialog is then reached via double-click. Used by the Color
    // panel. Default (toolbar) behaviour opens the dialog on a single click.
    void setActiveTargetMode(bool on);
    bool isForegroundActive() const { return m_activeTarget == Foreground; }

    QColor foregroundColor() const { return m_fg; }
    QColor backgroundColor() const { return m_bg; }
    QPoint fgCenter() const { return m_fgRect.center(); }
    QPoint bgCenter() const { return m_bgRect.center(); }
    QPoint swapCenter() const { return m_swapRect.center(); }
    QPoint resetCenter() const { return m_resetRect.center(); }
    int hoveredArea() const { return static_cast<int>(m_hovered); }

    QSize sizeHint() const override { return QSize(52, 58); }
    QSize minimumSizeHint() const override { return QSize(42, 50); }

signals:
    void activeTargetChanged(bool foreground);

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    enum Area { None = 0, Foreground = 1, Background = 2, Swap = 3, Reset = 4 };
    Area hitTest(const QPoint& widgetPos) const;
    void updateRects();
    void openPickerFor(Area area);

    ColorEngine* m_colorEngine;

    bool m_activeTargetMode = false;
    Area m_activeTarget = Foreground;

    QColor m_fg = Qt::black;
    QColor m_bg = Qt::white;

    QRect m_fgRect;
    QRect m_bgRect;
    QRect m_swapRect;
    QRect m_resetRect;

    Area m_hovered = None;

    QPointer<QDialog> m_fgPickerDlg;
    QPointer<QDialog> m_bgPickerDlg;
};
