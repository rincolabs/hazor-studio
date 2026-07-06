#include "ScrubbableValueInput.h"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QDoubleValidator>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QLocale>
#include <QSlider>
#include <QToolButton>
#include <algorithm>
#include <cmath>

ScrubbableValueInput::ScrubbableValueInput(const QString& label,
                                           double minValue,
                                           double maxValue,
                                           double value,
                                           const QString& suffix,
                                           double step,
                                           QWidget* parent)
    : QWidget(parent)
    , m_labelText(label)
    , m_suffix(suffix)
    , m_min(std::min(minValue, maxValue))
    , m_max(std::max(minValue, maxValue))
    , m_step(std::max(step, 0.000001))
{
    auto* t = ThemeManager::instance()->current();
    setCursor(Qt::SizeHorCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover, true);
    // setMinimumHeight(24);
    // setFixedHeight(28);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(t->spaceXS, 1, t->spaceXS, 1);
    // layout->setContentsMargins(0, 0, 0, 0);
    // layout->setSpacing(t->spaceXS);
    layout->setSpacing(0);

    m_label = new QLabel(this);
    m_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_label->setTextInteractionFlags(Qt::NoTextInteraction);
    layout->addWidget(m_label, 0);

    m_valueLabel = new QLabel(this);
    m_valueLabel->setObjectName(QStringLiteral("scrubbableValueLabel"));
    m_valueLabel->setAttribute(Qt::WA_StyledBackground, true);
    m_valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_valueLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    m_valueLabel->setMinimumWidth(42);
    m_valueLabel->setFixedHeight(24);
    layout->addWidget(m_valueLabel, 0);

    m_editor = new QLineEdit(this);
    m_editor->setAlignment(Qt::AlignRight);
    m_editor->setFrame(false);
    m_editor->setVisible(false);
    m_editor->setMinimumWidth(54);
    layout->addWidget(m_editor, 0);

    m_dropArrow = new QToolButton(this);
    m_dropArrow->setObjectName(QStringLiteral("ScrubbableArrowButton"));
    m_dropArrow->setText(QStringLiteral("▾"));
    m_dropArrow->setFixedWidth(20);
    m_dropArrow->setFixedHeight(24);
    m_dropArrow->setCursor(Qt::ArrowCursor);
    m_dropArrow->setFocusPolicy(Qt::NoFocus);
    m_dropArrow->setAutoRaise(true);
    // m_dropArrow->setStyleSheet(QStringLiteral(
    //     "ScrubbableArrowButton {"
    //     "  background: %4;"
    //     "  border: none;"
    //     "  color: %1;"
    //     "  font-size: 9px;"
    //     "  padding:0px;"
    //     "}"
    //     "ScrubbableArrowButton:hover {"
    //     "  background: %2;"
    //     "  border-radius: %3px;"
    //     "}"
    // )
        // .arg(t->colorTextSecondary.name(),
        //      t->colorSurfaceHover.name())
        // .arg(t->radiusSM)
        // .arg(t->colorBackgroundTertiary.name())
    // );
    layout->addWidget(m_dropArrow, 0);
    connect(m_dropArrow, &QToolButton::clicked, this, &ScrubbableValueInput::openSliderPopup);

    installEventFilter(this);
    m_label->installEventFilter(this);
    m_valueLabel->installEventFilter(this);
    m_editor->installEventFilter(this);
    updateValidator();
    applyValue(value, false);
    updateDisplay();

    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &ScrubbableValueInput::applyTheme);
    applyTheme();
}

void ScrubbableValueInput::setLabel(const QString& label)
{
    m_labelText = label;
    updateDisplay();
}

void ScrubbableValueInput::setValue(double value)
{
    applyValue(value, false);
}

void ScrubbableValueInput::setRange(double minValue, double maxValue)
{
    m_min = std::min(minValue, maxValue);
    m_max = std::max(minValue, maxValue);
    updateValidator();
    applyValue(m_value, false);
    updatePopupRange();
}

void ScrubbableValueInput::setStep(double step)
{
    m_step = std::max(step, 0.000001);
    applyValue(m_value, false);
    updatePopupRange();
}

void ScrubbableValueInput::setDecimals(int decimals)
{
    m_decimals = std::clamp(decimals, 0, 6);
    updateValidator();
    updateDisplay();
}

void ScrubbableValueInput::setSuffix(const QString& suffix)
{
    m_suffix = suffix;
    updateDisplay();
}

void ScrubbableValueInput::setSensitivity(double sensitivity)
{
    m_sensitivity = std::max(sensitivity, 0.000001);
}

void ScrubbableValueInput::setReadOnly(bool readOnly)
{
    m_readOnly = readOnly;
    setCursor(m_readOnly ? Qt::ArrowCursor : Qt::SizeHorCursor);
    if (m_dropArrow)
        m_dropArrow->setVisible(!m_readOnly);
}

bool ScrubbableValueInput::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_editor) {
        if (event->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(event);
            if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
                commitTextEdit();
                return true;
            }
            if (ke->key() == Qt::Key_Escape) {
                cancelTextEdit();
                return true;
            }
            return false;
        }
        if (event->type() == QEvent::FocusOut && m_editor->isVisible()) {
            commitTextEdit();
            return false;
        }
        return QWidget::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress:
        mousePressEvent(static_cast<QMouseEvent*>(event));
        return true;
    case QEvent::MouseMove:
        mouseMoveEvent(static_cast<QMouseEvent*>(event));
        return true;
    case QEvent::MouseButtonRelease:
        mouseReleaseEvent(static_cast<QMouseEvent*>(event));
        return true;
    case QEvent::MouseButtonDblClick:
        mouseDoubleClickEvent(static_cast<QMouseEvent*>(event));
        return true;
    default:
        return QWidget::eventFilter(watched, event);
    }
}

void ScrubbableValueInput::mousePressEvent(QMouseEvent* event)
{
    if (m_readOnly || m_editor->isVisible() || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_pressed = true;
    m_dragging = false;
    m_pressGlobalPos = event->globalPosition().toPoint();
    m_dragStartValue = m_value;
    grabMouse(Qt::SizeHorCursor);
    event->accept();
}

void ScrubbableValueInput::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_pressed || m_readOnly) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QPoint globalPos = event->globalPosition().toPoint();
    if (!m_dragging && std::abs(globalPos.x() - m_pressGlobalPos.x()) >= 2)
        beginDrag(globalPos);
    if (m_dragging)
        updateDrag(globalPos, event->modifiers());
    event->accept();
}

void ScrubbableValueInput::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !m_pressed) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    const bool wasDragging = m_dragging;
    endDrag();
    if (wasDragging)
        emit editingFinished(m_value);
    event->accept();
}

void ScrubbableValueInput::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_readOnly || event->button() != Qt::LeftButton) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }

    beginTextEdit();
    event->accept();
}

void ScrubbableValueInput::keyPressEvent(QKeyEvent* event)
{
    if (!m_editor->isVisible()) {
        QWidget::keyPressEvent(event);
        return;
    }

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        commitTextEdit();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        cancelTextEdit();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

QString ScrubbableValueInput::formatValue() const
{
    const QString valueText = QLocale().toString(m_value, 'f', m_decimals);
    return m_suffix.isEmpty() ? valueText : valueText + m_suffix;
}

double ScrubbableValueInput::clampValue(double value) const
{
    return std::clamp(value, m_min, m_max);
}

double ScrubbableValueInput::snapValue(double value) const
{
    if (m_step <= 0.0)
        return clampValue(value);
    const double snapped = m_min + std::round((value - m_min) / m_step) * m_step;
    return clampValue(snapped);
}

void ScrubbableValueInput::applyValue(double value, bool emitChange)
{
    const double next = snapValue(value);
    if (qFuzzyCompare(next + 1.0, m_value + 1.0)) {
        updateDisplay();
        return;
    }

    m_value = next;
    updateDisplay();
    syncPopupSlider();
    if (emitChange)
        emit valueChanged(m_value);
}

void ScrubbableValueInput::updateDisplay()
{
    if (m_label)
        m_label->setText(m_labelText.isEmpty() ? QString() : m_labelText + QStringLiteral(":"));
    if (m_valueLabel)
        m_valueLabel->setText(formatValue());
    setToolTip(m_readOnly ? QString() : tr("Drag horizontally to adjust. Double-click to edit."));
}

void ScrubbableValueInput::updateValidator()
{
    if (!m_editor)
        return;
    delete m_editor->validator();
    auto* validator = new QDoubleValidator(m_min, m_max, m_decimals, m_editor);
    validator->setNotation(QDoubleValidator::StandardNotation);
    m_editor->setValidator(validator);
}

void ScrubbableValueInput::beginDrag(const QPoint&)
{
    if (m_dragging)
        return;
    m_dragging = true;
    emit editingStarted();
}

void ScrubbableValueInput::updateDrag(const QPoint& globalPos, Qt::KeyboardModifiers modifiers)
{
    double multiplier = 1.0;
    if (modifiers & Qt::AltModifier)
        multiplier = 0.01;
    else if (modifiers & Qt::ShiftModifier)
        multiplier = 0.1;
    else if (modifiers & Qt::ControlModifier)
        multiplier = 10.0;

    const int dx = globalPos.x() - m_pressGlobalPos.x();
    const double delta = static_cast<double>(dx) * m_step * m_sensitivity * multiplier;
    applyValue(m_dragStartValue + delta, true);
}

void ScrubbableValueInput::endDrag()
{
    if (mouseGrabber() == this)
        releaseMouse();
    m_pressed = false;
    m_dragging = false;
}

void ScrubbableValueInput::beginTextEdit()
{
    if (m_editor->isVisible())
        return;

    m_editStartValue = m_value;
    m_pressed = false;
    m_dragging = false;
    if (mouseGrabber() == this)
        releaseMouse();

    emit editingStarted();
    m_label->setVisible(false);
    m_valueLabel->setVisible(false);
    m_editor->setVisible(true);
    if (m_dropArrow)
        m_dropArrow->setVisible(false);
    m_editor->setText(QLocale().toString(m_value, 'f', m_decimals));
    m_editor->selectAll();
    m_editor->setFocus(Qt::MouseFocusReason);
}

void ScrubbableValueInput::commitTextEdit()
{
    if (!m_editor->isVisible())
        return;

    bool ok = false;
    double parsed = QLocale().toDouble(m_editor->text(), &ok);
    if (!ok)
        parsed = m_editor->text().toDouble(&ok);
    if (!ok) {
        cancelTextEdit();
        return;
    }

    m_editor->setVisible(false);
    m_label->setVisible(true);
    m_valueLabel->setVisible(true);
    if (m_dropArrow)
        m_dropArrow->setVisible(true);
    applyValue(parsed, true);
    emit editingFinished(m_value);
}

void ScrubbableValueInput::cancelTextEdit()
{
    if (!m_editor->isVisible())
        return;

    m_editor->setVisible(false);
    m_label->setVisible(true);
    m_valueLabel->setVisible(true);
    if (m_dropArrow)
        m_dropArrow->setVisible(true);
    applyValue(m_editStartValue, false);
    emit editingCanceled();
}

void ScrubbableValueInput::applyTheme()
{
    auto* t = ThemeManager::instance()->current();
    const QString controlBg = t->colorBackgroundTertiary.name();
    const QString buttonBg = t->colorSurface.name();
    const QString hoverBg = t->colorSurfaceHover.name();
    const QString accent = t->colorAccent.name();
    const QString text = t->colorTextPrimary.name();
    const QString border = t->colorBorderLight.name();

    setStyleSheet(QStringLiteral(
        "ScrubbableValueInput {"
        "  background: %1;"
        "  border: 1px solid %2;"
        "  border-radius: %3px;"
        "  padding: 2px 4px;"
        "}"
        "ScrubbableValueInput:hover {"
        "  background: %4;"
        "  border-color: %5;"
        "  padding: 2px 4px;"
        "}"
        "QToolButton {"
        "  background: %6;"
        "  border: 1px solid %2;"
        "  color: %7;"
        "  font-size: 9px;"
        "  padding: 0;"
        "  margin: 0;"
        "  border-radius: 0;"
        "}"
        "QToolButton:hover {"
        "  background: %4;"
        "}"
        "QLabel {"
        "  color: %7;"
        "  font-size: %8px;"
        "  padding: 2px 2px;"
        "}"
        "QLabel#scrubbableValueLabel {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: %3px;"
        "  padding: 2px 2px 2px 0;"
        "}"
        "QLineEdit {"
        "  background: %1;"
        "  color: %7;"
        "  border: 1px solid %5;"
        "  border-radius: %3px;"
        "  padding: 2px 2px;"
        "  selection-background-color: %5;"
        "}")
        .arg(controlBg, border)
        .arg(t->radiusSM)
        .arg(hoverBg, accent, buttonBg, text)
        .arg(t->fontSizeSM));

    if (m_sliderPopup) {
        m_sliderPopup->setStyleSheet(QStringLiteral(
            "#ScrubbableSliderPopup {"
            "  background: %1;"
            "  border: 1px solid %2;"
            "  border-radius: %3px;"
            "}"
            "QSlider::groove:horizontal {"
            "  height: 4px;"
            "  background: %4;"
            "  border-radius: 2px;"
            "}"
            "QSlider::handle:horizontal {"
            "  background: %5;"
            "  width: 12px;"
            "  height: 12px;"
            "  margin: -4px 0;"
            "  border-radius: 6px;"
            "}"
            "QSlider::sub-page:horizontal {"
            "  background: %5;"
            "  border-radius: 2px;"
            "}")
            .arg(t->colorBackgroundSecondary.name(),
                 t->colorBorder.name())
            .arg(t->radiusMD)
            .arg(controlBg, accent));
    }
}

void ScrubbableValueInput::openSliderPopup()
{
    if (m_editor->isVisible())
        return;

    if (!m_sliderPopup) {
        m_sliderPopup = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint);
        m_sliderPopup->setObjectName(QStringLiteral("ScrubbableSliderPopup"));
        m_sliderPopup->setFixedHeight(36);
        m_sliderPopup->setAttribute(Qt::WA_StyledBackground, true);

        auto* popupLayout = new QHBoxLayout(m_sliderPopup);
        popupLayout->setContentsMargins(8, 6, 8, 6);
        popupLayout->setSpacing(0);

        m_popupSlider = new QSlider(Qt::Horizontal, m_sliderPopup);
        m_popupSlider->setFixedWidth(200);
        m_popupSlider->setFocusPolicy(Qt::NoFocus);
        popupLayout->addWidget(m_popupSlider);

        connect(m_popupSlider, &QSlider::valueChanged, this, [this](int pos) {
            applyValue(sliderToValue(pos), true);
        });
        applyTheme();
    }

    updatePopupRange();
    syncPopupSlider();

    const QPoint globalPos = mapToGlobal(QPoint(0, height()));
    m_sliderPopup->move(globalPos);
    m_sliderPopup->show();
    m_sliderPopup->raise();
}

void ScrubbableValueInput::syncPopupSlider()
{
    if (!m_popupSlider || !m_sliderPopup || !m_sliderPopup->isVisible())
        return;
    m_popupSlider->blockSignals(true);
    m_popupSlider->setValue(valueToSlider(m_value));
    m_popupSlider->blockSignals(false);
}

void ScrubbableValueInput::updatePopupRange()
{
    if (!m_popupSlider)
        return;
    const int steps = sliderSteps();
    m_popupSlider->blockSignals(true);
    m_popupSlider->setRange(0, steps);
    m_popupSlider->setSingleStep(1);
    m_popupSlider->setPageStep(std::max(1, steps / 10));
    m_popupSlider->setValue(valueToSlider(m_value));
    m_popupSlider->blockSignals(false);
}

int ScrubbableValueInput::sliderSteps() const
{
    if (m_step <= 0.0)
        return 100;
    return static_cast<int>(std::round((m_max - m_min) / m_step));
}

int ScrubbableValueInput::valueToSlider(double v) const
{
    if (m_step <= 0.0)
        return 0;
    return static_cast<int>(std::round((v - m_min) / m_step));
}

double ScrubbableValueInput::sliderToValue(int pos) const
{
    return m_min + static_cast<double>(pos) * m_step;
}
