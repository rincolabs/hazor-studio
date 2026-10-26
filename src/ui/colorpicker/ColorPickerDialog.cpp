#include "ColorPickerDialog.hpp"
#include "SaturationBrightnessArea.hpp"
#include "ColorHueSlider.hpp"
#include "ColorPreviewWidget.hpp"
#include "ColorFieldsWidget.hpp"
#include "CanvasColorPickSource.hpp"

#include "renderer/CanvasView.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "ui/IconUtils.hpp"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPushButton>
#include <QShowEvent>
#include <QToolButton>

ColorPickerDialog::ColorPickerDialog(const QColor& initialColor,
                                     ColorPickerMode mode,
                                     QWidget* parent)
    : QDialog(parent)
    , m_mode(mode)
    , m_initialColor(initialColor)
    , m_selectedColor(initialColor)
{
    QString modeText = (mode == ColorPickerMode::Foreground)
        ? tr("Foreground Color") : tr("Background Color");
    setWindowTitle(tr("Color Picker (%1)").arg(modeText));
    setMinimumSize(560, 400);
    // Matches the filter dialogs (execToolDialog): Qt::Tool gives this dialog a
    // movable title bar instead of the fixed one plain Qt::Dialog gets here.
    setWindowFlags(windowFlags() | Qt::Tool);

    auto* t = ThemeManager::instance()->current();

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(t->spaceLG, t->spaceLG, t->spaceLG, t->spaceLG);
    mainLayout->setSpacing(t->spaceMD);

    auto* topLayout = new QHBoxLayout;
    topLayout->setSpacing(t->spaceMD);

    // Left: saturation/value area
    m_satBrightArea = new SaturationBrightnessArea(this);
    m_satBrightArea->setMinimumSize(200, 200);
    topLayout->addWidget(m_satBrightArea, 1);

    // Vertical hue slider
    m_hueSlider = new ColorHueSlider(this);
    m_hueSlider->setMinimumSize(24, 200);
    m_hueSlider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    topLayout->addWidget(m_hueSlider, 0);

    // Right column: preview + action buttons on top, numeric fields below.
    auto* rightLayout = new QVBoxLayout;
    rightLayout->setSpacing(t->spaceMD);

    auto* previewButtonsRow = new QHBoxLayout;
    previewButtonsRow->setSpacing(t->spaceMD);

    auto* previewCol = new QVBoxLayout;
    previewCol->setSpacing(t->spaceSM);

    m_previewWidget = new ColorPreviewWidget(this);
    m_previewWidget->setCurrentColor(m_initialColor);
    m_previewWidget->setNewColor(m_selectedColor);
    previewCol->addWidget(m_previewWidget, 0, Qt::AlignTop);

    // Eyedropper: arms the active canvas so a press/drag over the image feeds
    // its pixels straight into this dialog (and through colorPreviewChanged,
    // into whatever the dialog is previewing).
    m_eyedropperBtn = new QToolButton(this);
    m_eyedropperBtn->setCheckable(true);
    m_eyedropperBtn->setAutoRaise(true);
    m_eyedropperBtn->setIcon(makeIcon(QStringLiteral(":/icons/eyedropper.png")));
    m_eyedropperBtn->setIconSize(QSize(20, 20));
    previewCol->addWidget(m_eyedropperBtn, 0, Qt::AlignLeft);
    previewCol->addStretch();

    previewButtonsRow->addLayout(previewCol);

    auto* buttonCol = new QVBoxLayout;
    buttonCol->setSpacing(t->spaceSM);

    auto* okBtn = new QPushButton(tr("OK"), this);
    okBtn->setObjectName("okBtn");
    okBtn->setDefault(true);
    auto* cancelBtn = new QPushButton(tr("Cancel"), this);
    cancelBtn->setObjectName("cancelBtn");
    auto* swatchesBtn = new QPushButton(tr("Add to Swatches"), this);
    auto* librariesBtn = new QPushButton(tr("Color Libraries"), this);

    buttonCol->addWidget(okBtn);
    buttonCol->addWidget(cancelBtn);
    buttonCol->addWidget(swatchesBtn);
    buttonCol->addWidget(librariesBtn);
    buttonCol->addStretch();
    previewButtonsRow->addLayout(buttonCol);

    rightLayout->addLayout(previewButtonsRow);

    m_fieldsWidget = new ColorFieldsWidget(this);
    rightLayout->addWidget(m_fieldsWidget);
    rightLayout->addStretch();

    topLayout->addLayout(rightLayout, 0);

    mainLayout->addLayout(topLayout, 1);

    setStyleSheet(t->globalStyleSheet());
    setAttribute(Qt::WA_StyledBackground);

    connect(m_satBrightArea, &SaturationBrightnessArea::colorChanged,
            this, &ColorPickerDialog::onVisualColorChanged);
    connect(m_hueSlider, &ColorHueSlider::hueChanged,
            this, &ColorPickerDialog::onHueSliderChanged);
    connect(m_fieldsWidget, &ColorFieldsWidget::colorChanged,
            this, &ColorPickerDialog::onFieldsColorChanged);
    connect(m_eyedropperBtn, &QToolButton::toggled,
            this, &ColorPickerDialog::onEyedropperToggled);

    connect(okBtn, &QPushButton::clicked, this, [this]() {
        emit colorAccepted(m_selectedColor);
        accept();
    });
    connect(cancelBtn, &QPushButton::clicked, this, &ColorPickerDialog::reject);
    connect(swatchesBtn, &QPushButton::clicked, this, [this]() {
        emit swatchAddRequested(m_selectedColor);
    });
    connect(librariesBtn, &QPushButton::clicked, this, [this]() {
        emit colorLibrariesRequested();
    });

    updateEyedropperAvailability();
    syncAllWidgets();
}

ColorPickerDialog::~ColorPickerDialog()
{
    // Destroyed without ever closing (its parent went away): the canvas must
    // not stay armed for a dialog that no longer exists.
    if (m_pickCanvas)
        m_pickCanvas->setColorPickArmed(false);
}

void ColorPickerDialog::showModeless()
{
    setWindowModality(Qt::NonModal);
    setModal(false);
    setResult(0);
    show();
    raise();
    activateWindow();
}

void ColorPickerDialog::reject()
{
    // Cancel must leave no trace. Re-emitting the original colour restores every
    // live preview listening on colorPreviewChanged, and routing the Cancel
    // button, Esc and the title bar's close button through this one override
    // means none of them can skip it.
    if (m_selectedColor != m_initialColor) {
        m_selectedColor = m_initialColor;
        emit colorPreviewChanged(m_initialColor);
    }
    QDialog::reject();
}

void ColorPickerDialog::done(int result)
{
    disarmEyedropper();
    QDialog::done(result);
}

void ColorPickerDialog::showEvent(QShowEvent* e)
{
    QDialog::showEvent(e);
    // Modality is only known once the dialog is being shown (QDialog::exec sets
    // it on the way in), and it decides whether a pick can reach the canvas.
    updateEyedropperAvailability();
}

void ColorPickerDialog::keyPressEvent(QKeyEvent* e)
{
    // While the eyedropper is armed Esc calls off the pick, not the dialog.
    if (e->key() == Qt::Key_Escape && m_pickCanvas) {
        onCanvasPickCancelled();
        e->accept();
        return;
    }
    QDialog::keyPressEvent(e);
}

void ColorPickerDialog::onVisualColorChanged(const QColor& color)
{
    if (m_updatingUI) return;
    m_selectedColor = color;
    emit colorPreviewChanged(m_selectedColor);
    syncAllWidgets();
}

void ColorPickerDialog::onHueSliderChanged(double hue)
{
    if (m_updatingUI) return;
    double s = m_selectedColor.saturationF();
    double v = m_selectedColor.valueF();
    if (s < 0.0) s = 1.0;
    if (v < 0.0) v = 1.0;
    m_selectedColor = QColor::fromHsvF(hue / 360.0, s, v);
    emit colorPreviewChanged(m_selectedColor);
    syncAllWidgets();
}

void ColorPickerDialog::onFieldsColorChanged(const QColor& color)
{
    if (m_updatingUI) return;
    m_selectedColor = color;
    emit colorPreviewChanged(m_selectedColor);
    syncAllWidgets();
}

// ── Eyedropper ───────────────────────────────────────────────────────────────

void ColorPickerDialog::onEyedropperToggled(bool on)
{
    if (on)
        armEyedropper();
    else
        disarmEyedropper();
}

void ColorPickerDialog::armEyedropper()
{
    CanvasView* canvas = CanvasColorPickSource::canvas();
    if (!canvas) {
        // The document was closed while the dialog stayed open: re-evaluate, which
        // disables the button and un-presses it.
        updateEyedropperAvailability();
        return;
    }

    m_colorBeforePick = m_selectedColor;
    m_pickCanvas = canvas;
    connect(canvas, &CanvasView::colorPickSampled,
            this, &ColorPickerDialog::onCanvasColorSampled);
    connect(canvas, &CanvasView::colorPickFinished,
            this, &ColorPickerDialog::onCanvasPickFinished);
    connect(canvas, &CanvasView::colorPickCancelled,
            this, &ColorPickerDialog::onCanvasPickCancelled);
    // Closing the document under an armed eyedropper must not leave the button
    // pressed with nothing behind it.
    connect(canvas, &QObject::destroyed, this, [this]() { disarmEyedropper(); });
    canvas->setColorPickArmed(true);
    emit eyedropperArmedChanged(true);
}

void ColorPickerDialog::disarmEyedropper()
{
    const bool wasArmed = (m_pickCanvas != nullptr);
    if (m_pickCanvas) {
        m_pickCanvas->setColorPickArmed(false);
        // Only this dialog's own pick connections live on that canvas.
        disconnect(m_pickCanvas, nullptr, this, nullptr);
        m_pickCanvas = nullptr;
    }
    if (m_eyedropperBtn && m_eyedropperBtn->isChecked()) {
        QSignalBlocker blocker(m_eyedropperBtn);
        m_eyedropperBtn->setChecked(false);
    }
    if (wasArmed)
        emit eyedropperArmedChanged(false);
}

void ColorPickerDialog::onCanvasColorSampled(const QColor& color)
{
    setColorFromPick(color);
}

void ColorPickerDialog::onCanvasPickFinished(const QColor& color)
{
    setColorFromPick(color);
    disarmEyedropper();
    // Clicking the canvas gave the main window the focus; come back to the front
    // so the dialog the user is working in is not left buried behind it.
    raise();
    activateWindow();
}

void ColorPickerDialog::onCanvasPickCancelled()
{
    // Esc, or the canvas disarming itself (tool switch): the pick is called off,
    // so the colour the dialog had when it was armed comes back — anything
    // sampled during the cancelled drag was never confirmed.
    const QColor restore = m_colorBeforePick;
    disarmEyedropper();
    if (restore.isValid())
        setColorFromPick(restore);
}

void ColorPickerDialog::setColorFromPick(const QColor& color)
{
    if (!color.isValid() || color == m_selectedColor)
        return;
    m_selectedColor = color;
    emit colorPreviewChanged(m_selectedColor);
    syncAllWidgets();
}

void ColorPickerDialog::updateEyedropperAvailability()
{
    if (!m_eyedropperBtn)
        return;

    const bool hasCanvas = CanvasColorPickSource::available();
    // A modal dialog blocks every click on the canvas, so an armed eyedropper
    // could never receive one — better a disabled button that says why than a
    // dead control. (The dialogs whose colour the canvas shows live — text,
    // shapes, Solid Color, layer styles — are all opened non-modally.)
    const bool blocked = isModal() || QApplication::activeModalWidget() != nullptr;
    const bool canPick = hasCanvas && !blocked;

    m_eyedropperBtn->setEnabled(canPick);
    if (canPick) {
        m_eyedropperBtn->setToolTip(tr("Pick a colour from the image"));
    } else if (!hasCanvas) {
        m_eyedropperBtn->setToolTip(tr("Open an image to pick a colour from it"));
    } else {
        m_eyedropperBtn->setToolTip(
            tr("Picking from the image is not available from this dialog"));
    }
    if (!canPick)
        disarmEyedropper();
}

void ColorPickerDialog::syncAllWidgets()
{
    m_updatingUI = true;

    m_satBrightArea->setColor(m_selectedColor);
    double h = m_selectedColor.hueF();
    if (h < 0.0) h = 0.0;
    m_hueSlider->setHue(h * 360.0);
    m_previewWidget->setNewColor(m_selectedColor);
    m_fieldsWidget->setColor(m_selectedColor);

    m_updatingUI = false;
}
