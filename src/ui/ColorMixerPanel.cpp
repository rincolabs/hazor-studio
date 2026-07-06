#include "ColorMixerPanel.hpp"

#include "core/ColorEngine.hpp"
#include "ColorSwapWidget.hpp"
#include "colorpicker/SaturationBrightnessArea.hpp"
#include "colorpicker/ColorHueSlider.hpp"

#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QGridLayout>

ColorMixerPanel::ColorMixerPanel(ColorEngine* engine, QWidget* parent)
    : QWidget(parent)
    , m_engine(engine)
{
    auto* t = ThemeManager::instance()->current();
    setObjectName(QStringLiteral("colorMixerPanel"));
    setAttribute(Qt::WA_StyledBackground, true);

    // 3-column grid keeps the FG/BG swatches alongside the picker (saves header
    // height): col 0 = swatches (component width), col 1 = SV area (stretches),
    // col 2 = vertical Hue slider.
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(t->spaceMD, t->spaceMD, t->spaceMD, t->spaceMD);
    grid->setHorizontalSpacing(t->spaceSM);
    grid->setVerticalSpacing(t->spaceSM);

    // Foreground / Background swatches (reused component, active-target mode).
    m_swap = new ColorSwapWidget(m_engine, this);
    m_swap->setActiveTargetMode(true);
    m_swap->setToolTip(tr("Click to select the colour to edit; double-click to open the picker"));
    grid->addWidget(m_swap, 0, 0, Qt::AlignTop);

    m_svArea = new SaturationBrightnessArea(this);
    m_svArea->setMinimumSize(120, 120);
    m_svArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    grid->addWidget(m_svArea, 0, 1);

    m_hueSlider = new ColorHueSlider(this);
    m_hueSlider->setMinimumWidth(16);
    m_hueSlider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    grid->addWidget(m_hueSlider, 0, 2);

    grid->setColumnStretch(0, 0);  // swatches: fixed to component width
    grid->setColumnStretch(1, 1);  // SV area absorbs extra width
    grid->setColumnStretch(2, 0);  // hue slider: fixed

    connect(m_svArea, &SaturationBrightnessArea::colorChanged,
            this, &ColorMixerPanel::onSvAreaChanged);
    connect(m_hueSlider, &ColorHueSlider::hueChanged,
            this, &ColorMixerPanel::onHueChanged);
    connect(m_swap, &ColorSwapWidget::activeTargetChanged,
            this, &ColorMixerPanel::onActiveTargetChanged);

    if (m_engine) {
        connect(m_engine, &ColorEngine::foregroundColorChanged, this, [this](const QColor&) {
            if (m_activeForeground) syncFromEngine();
        });
        connect(m_engine, &ColorEngine::backgroundColorChanged, this, [this](const QColor&) {
            if (!m_activeForeground) syncFromEngine();
        });
    }

    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &ColorMixerPanel::applyTheme);
    applyTheme();
    syncFromEngine();
}

QColor ColorMixerPanel::activeColor() const
{
    if (!m_engine) return Qt::black;
    return m_activeForeground ? m_engine->foregroundColor()
                              : m_engine->backgroundColor();
}

void ColorMixerPanel::onSvAreaChanged(const QColor& color)
{
    if (m_syncing) return;
    m_hue = m_svArea->hue();
    pushColor(color);
}

void ColorMixerPanel::onHueChanged(double hue)
{
    if (m_syncing) return;
    m_hue = hue;
    m_svArea->setHue(hue);
    QColor c = QColor::fromHsvF(hue / 360.0,
                               m_svArea->saturation() / 100.0,
                               m_svArea->brightness() / 100.0);
    pushColor(c);
}

void ColorMixerPanel::onActiveTargetChanged(bool foreground)
{
    if (m_activeForeground == foreground) return;
    m_activeForeground = foreground;
    syncFromEngine();
}

void ColorMixerPanel::pushColor(const QColor& color)
{
    if (!m_engine) return;

    // Preserve the active target's existing alpha; the SV/Hue widgets are opaque.
    QColor c = color;
    c.setAlpha(activeColor().alpha());

    // Live edits don't pollute Recent Colors (standard behaviour).
    if (m_activeForeground)
        m_engine->setForegroundColor(c, /*addToRecent*/ false);
    else
        m_engine->setBackgroundColor(c, /*addToRecent*/ false);
}

void ColorMixerPanel::syncFromEngine()
{
    m_syncing = true;

    const QColor c = activeColor();
    if (c.saturationF() > 0.0 && c.valueF() > 0.0)
        m_hue = c.hueF() * 360.0;
    // else keep the previously preserved hue so grays/blacks don't snap to red.

    m_svArea->setColor(c);
    m_svArea->setHue(m_hue);
    m_hueSlider->setHue(m_hue);

    m_syncing = false;
}

void ColorMixerPanel::applyTheme()
{
    auto* t = ThemeManager::instance()->current();
    setStyleSheet(QStringLiteral("ColorMixerPanel { background: %1; color: %2; }")
        .arg(t->colorSurface.name(), t->colorTextPrimary.name()));
    update();
}
