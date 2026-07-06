#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QToolButton;
class QFrame;
class QSlider;

class ScrubbableValueInput : public QWidget {
    Q_OBJECT

public:
    explicit ScrubbableValueInput(const QString& label = QString(),
                                  double minValue = 0.0,
                                  double maxValue = 100.0,
                                  double value = 0.0,
                                  const QString& suffix = QString(),
                                  double step = 1.0,
                                  QWidget* parent = nullptr);

    void setLabel(const QString& label);
    void setValue(double value);
    double value() const { return m_value; }
    void setRange(double minValue, double maxValue);
    void setStep(double step);
    void setDecimals(int decimals);
    void setSuffix(const QString& suffix);
    void setSensitivity(double sensitivity);
    void setReadOnly(bool readOnly);

signals:
    void valueChanged(double value);
    void editingStarted();
    void editingFinished(double value);
    void editingCanceled();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QString formatValue() const;
    double clampValue(double value) const;
    double snapValue(double value) const;
    void applyValue(double value, bool emitChange);
    void updateDisplay();
    void updateValidator();
    void beginDrag(const QPoint& globalPos);
    void updateDrag(const QPoint& globalPos, Qt::KeyboardModifiers modifiers);
    void endDrag();
    void beginTextEdit();
    void commitTextEdit();
    void cancelTextEdit();
    void applyTheme();
    void openSliderPopup();
    void syncPopupSlider();
    void updatePopupRange();
    int sliderSteps() const;
    int valueToSlider(double v) const;
    double sliderToValue(int pos) const;

    QLabel* m_label = nullptr;
    QLabel* m_valueLabel = nullptr;
    QLineEdit* m_editor = nullptr;
    QToolButton* m_dropArrow = nullptr;
    QFrame* m_sliderPopup = nullptr;
    QSlider* m_popupSlider = nullptr;

    QString m_labelText;
    QString m_suffix;
    double m_min = 0.0;
    double m_max = 100.0;
    double m_step = 1.0;
    double m_value = 0.0;
    double m_sensitivity = 1.0;
    double m_dragStartValue = 0.0;
    double m_editStartValue = 0.0;
    int m_decimals = 0;
    bool m_readOnly = false;
    bool m_pressed = false;
    bool m_dragging = false;
    QPoint m_pressGlobalPos;
};
