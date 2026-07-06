#include "BrushTexturePanel.hpp"
#include "AppCheckBox.hpp"
#include "BrushResourceBrowser.hpp"
#include "brush/BrushTextureLibrary.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTabWidget>
#include <QLabel>
#include <QComboBox>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QFileDialog>
#include <QPainter>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QPolygon>
#include <QLinearGradient>
#include <algorithm>
#include <cmath>

// ── CutoffRangeSlider ─────────────────────────────────────────

CutoffRangeSlider::CutoffRangeSlider(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(22);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize CutoffRangeSlider::sizeHint() const { return QSize(160, 22); }

void CutoffRangeSlider::setRange(float lo, float hi)
{
    m_low = std::clamp(lo, 0.0f, 1.0f);
    m_high = std::clamp(hi, 0.0f, 1.0f);
    if (m_low > m_high) std::swap(m_low, m_high);
    update();
}

QRect CutoffRangeSlider::barRect() const
{
    return rect().adjusted(6, 4, -6, -8);   // leave room for handles below
}

float CutoffRangeSlider::xToValue(int x) const
{
    const QRect b = barRect();
    if (b.width() <= 0) return 0.0f;
    return std::clamp(float(x - b.left()) / b.width(), 0.0f, 1.0f);
}

int CutoffRangeSlider::valueToX(float v) const
{
    const QRect b = barRect();
    return b.left() + int(std::round(v * b.width()));
}

void CutoffRangeSlider::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRect b = barRect();

    QLinearGradient grad(b.topLeft(), b.topRight());
    grad.setColorAt(0.0, Qt::black);
    grad.setColorAt(1.0, Qt::white);
    p.setPen(Qt::NoPen);
    p.fillRect(b, grad);
    p.setPen(QPen(QColor(0, 0, 0, 120), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(b);

    const Theme* t = ThemeManager::instance()->current();
    const QColor handle = t ? t->colorAccent : QColor(80, 140, 255);
    auto drawHandle = [&](float v) {
        const int x = valueToX(v);
        QPolygon tri;
        tri << QPoint(x, b.bottom() + 1)
            << QPoint(x - 4, b.bottom() + 7)
            << QPoint(x + 4, b.bottom() + 7);
        p.setBrush(handle);
        p.setPen(QPen(QColor(0, 0, 0, 160), 1));
        p.drawPolygon(tri);
    };
    drawHandle(m_low);
    drawHandle(m_high);
}

void CutoffRangeSlider::mousePressEvent(QMouseEvent* e)
{
    const float v = xToValue(int(e->position().x()));
    m_drag = (std::abs(v - m_low) <= std::abs(v - m_high)) ? 0 : 1;
    mouseMoveEvent(e);
}

void CutoffRangeSlider::mouseMoveEvent(QMouseEvent* e)
{
    if (m_drag < 0) return;
    const float v = xToValue(int(e->position().x()));
    if (m_drag == 0) m_low = std::min(v, m_high);
    else             m_high = std::max(v, m_low);
    update();
    emit rangeChanged(m_low, m_high);
}

void CutoffRangeSlider::mouseReleaseEvent(QMouseEvent*) { m_drag = -1; }

// ── BrushTexturePanel ─────────────────────────────────────────

namespace {
const char* kModeNames[] = {
    "Multiply", "Subtract", "Lightness Map", "Gradient Map", "Darken", "Overlay",
    "Color Dodge", "Color Burn", "Linear Burn", "Height", "Linear Height"};
const char* kCutoffPolicyNames[] = {
    "Cut Off Disabled", "Cut Off Pattern", "Cut Off Brush Mask", "Cut Off Both"};
} // namespace

BrushTexturePanel::BrushTexturePanel(QWidget* parent) : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildTextureTab(), tr("Texture"));
    m_tabs->addTab(buildOptionsTab(), tr("Options"));
    root->addWidget(m_tabs);

    connect(BrushTextureLibrary::instance(), &BrushTextureLibrary::texturesChanged,
            this, &BrushTexturePanel::reloadTextures);

    BrushTextureLibrary::instance()->reload();
    reloadTextures();
}

QWidget* BrushTexturePanel::buildTextureTab()
{
    m_browser = new BrushResourceBrowser(BrushResourceBrowserMode::BrushTexture, this);

    connect(m_browser, &BrushResourceBrowser::selected, this,
            [this](const QString& id) { onTextureSelected(id); });
    connect(m_browser, &BrushResourceBrowser::importRequested, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Import Texture"), {},
            tr("Images (*.png *.jpg *.jpeg *.bmp *.webp)"));
        if (path.isEmpty()) return;
        const QString id = BrushTextureLibrary::instance()->importFromFile(path);
        if (!id.isEmpty())
            onTextureSelected(id);   // select the freshly imported one
    });
    connect(m_browser, &BrushResourceBrowser::removeRequested, this,
            [this](const QString& id) {
        if (!id.isEmpty())
            BrushTextureLibrary::instance()->remove(id);
    });

    return m_browser;
}

QWidget* BrushTexturePanel::buildOptionsTab()
{
    const Theme* t = ThemeManager::instance()->current();
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    form->setContentsMargins(t->spaceSM, t->spaceSM, t->spaceSM, t->spaceSM);
    form->setSpacing(t->spaceSM);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    // Texturing Mode + Soft.
    {
        auto* row = new QWidget(page);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        m_mode = new QComboBox(row);
        for (const char* n : kModeNames) m_mode->addItem(tr(n));
        m_soft = new AppCheckBox(tr("Soft texturing"), row);
        h->addWidget(m_mode, 1);
        h->addWidget(m_soft);
        form->addRow(tr("Texturing Mode:"), row);
    }

    auto addSliderSpin = [&](const QString& label, QSlider*& slider,
                             QDoubleSpinBox*& spin, int sMin, int sMax,
                             double dMin, double dMax, double step, int factor) {
        auto* row = new QWidget(page);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(sMin, sMax);
        spin = new QDoubleSpinBox(row);
        spin->setRange(dMin, dMax);
        spin->setSingleStep(step);
        spin->setDecimals(2);
        spin->setFixedWidth(64);
        h->addWidget(slider, 1);
        h->addWidget(spin);
        form->addRow(label, row);
        connect(slider, &QSlider::valueChanged, this, [this, spin, factor](int v) {
            if (m_loading) return;
            QSignalBlocker b(spin);
            spin->setValue(double(v) / factor);
            emitChanged();
        });
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this, slider, factor](double d) {
            if (m_loading) return;
            QSignalBlocker b(slider);
            slider->setValue(int(std::round(d * factor)));
            emitChanged();
        });
    };

    // Scale (+ unit presets).
    {
        auto* row = new QWidget(page);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        m_scale = new QSlider(Qt::Horizontal, row);
        m_scale->setRange(10, 400);
        m_scaleSpin = new QDoubleSpinBox(row);
        m_scaleSpin->setRange(0.10, 4.00);
        m_scaleSpin->setSingleStep(0.05);
        m_scaleSpin->setDecimals(2);
        m_scaleSpin->setFixedWidth(64);
        m_scaleUnit = new QComboBox(row);
        for (double v : {0.10, 0.25, 0.50, 1.00, 1.50, 2.00, 4.00})
            m_scaleUnit->addItem(QString::number(v, 'f', 2), v);
        m_scaleUnit->setFixedWidth(64);
        h->addWidget(m_scale, 1);
        h->addWidget(m_scaleSpin);
        h->addWidget(m_scaleUnit);
        form->addRow(tr("Scale:"), row);
        connect(m_scale, &QSlider::valueChanged, this, [this](int v) {
            if (m_loading) return;
            QSignalBlocker b(m_scaleSpin);
            m_scaleSpin->setValue(double(v) / 100.0);
            emitChanged();
        });
        connect(m_scaleSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this](double d) {
            if (m_loading) return;
            QSignalBlocker b(m_scale);
            m_scale->setValue(int(std::round(d * 100.0)));
            emitChanged();
        });
        connect(m_scaleUnit, qOverload<int>(&QComboBox::activated), this, [this](int) {
            if (m_loading) return;
            m_scaleSpin->setValue(m_scaleUnit->currentData().toDouble());
        });
    }

    addSliderSpin(tr("Brightness:"), m_brightness, m_brightnessSpin,
                  -100, 100, -1.00, 1.00, 0.05, 100);
    addSliderSpin(tr("Contrast:"), m_contrast, m_contrastSpin,
                  0, 200, 0.00, 2.00, 0.05, 100);
    addSliderSpin(tr("Neutral point:"), m_neutral, m_neutralSpin,
                  0, 100, 0.00, 1.00, 0.05, 100);

    // Cutoff policy.
    m_cutoffPolicy = new QComboBox(page);
    for (const char* n : kCutoffPolicyNames) m_cutoffPolicy->addItem(tr(n));
    form->addRow(tr("Cutoff Policy:"), m_cutoffPolicy);
    connect(m_cutoffPolicy, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { if (!m_loading) emitChanged(); });

    // Cutoff range.
    m_cutoff = new CutoffRangeSlider(page);
    form->addRow(tr("Cutoff:"), m_cutoff);
    connect(m_cutoff, &CutoffRangeSlider::rangeChanged, this,
            [this](float, float) { if (!m_loading) emitChanged(); });

    // Offsets.
    auto addOffsetRow = [&](const QString& label, QSpinBox*& spin, AppCheckBox*& rnd) {
        auto* row = new QWidget(page);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        spin = new QSpinBox(row);
        spin->setRange(-2048, 2048);
        spin->setSuffix(QStringLiteral(" px"));
        rnd = new AppCheckBox(tr("Random Offset"), row);
        h->addWidget(spin);
        h->addWidget(rnd);
        h->addStretch();
        form->addRow(label, row);
        connect(spin, qOverload<int>(&QSpinBox::valueChanged), this,
                [this](int) { if (!m_loading) emitChanged(); });
        connect(rnd, &QCheckBox::toggled, this,
                [this](bool) { if (!m_loading) emitChanged(); });
    };
    addOffsetRow(tr("Horizontal Offset:"), m_hOffset, m_hRandom);
    addOffsetRow(tr("Vertical Offset:"), m_vOffset, m_vRandom);

    // Final checkboxes.
    m_invert = new AppCheckBox(tr("Invert Pattern"), page);
    m_autoInvertEraser = new AppCheckBox(tr("Auto Invert For Eraser"), page);
    form->addRow(QString(), m_invert);
    form->addRow(QString(), m_autoInvertEraser);
    connect(m_invert, &QCheckBox::toggled, this,
            [this](bool) { if (!m_loading) emitChanged(); });
    connect(m_autoInvertEraser, &QCheckBox::toggled, this,
            [this](bool) { if (!m_loading) emitChanged(); });

    connect(m_mode, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { if (!m_loading) emitChanged(); });
    connect(m_soft, &QCheckBox::toggled, this,
            [this](bool) { if (!m_loading) emitChanged(); });

    return page;
}

void BrushTexturePanel::reloadTextures()
{
    if (!m_browser) return;
    QVector<BrushResourceBrowser::Item> items;
    for (const BrushTexture& tex : BrushTextureLibrary::instance()->textures())
        items.push_back({tex.id, tex.name, tex.image});
    m_browser->setItems(items);
    m_browser->setSelectedId(m_config.textureId);
}

void BrushTexturePanel::onTextureSelected(const QString& id)
{
    const BrushTexture* tex = BrushTextureLibrary::instance()->find(id);
    m_config.textureId = id;
    m_config.texture = tex ? tex->image : QImage();
    // Reflect the selection in the grid (matters for the import path, where the
    // freshly added item must become selected; a no-op for plain grid clicks).
    if (m_browser) m_browser->setSelectedId(id);
    emitChanged();
}

void BrushTexturePanel::setConfig(const TextureConfig& c)
{
    m_loading = true;
    m_config = c;

    if (m_mode) m_mode->setCurrentIndex(std::clamp(int(c.texturingMode), 0, 10));
    if (m_soft) m_soft->setChecked(c.softTexturing);
    if (m_scale) m_scale->setValue(std::clamp(int(std::round(c.scale * 100.0f)), 10, 400));
    if (m_scaleSpin) { QSignalBlocker b(m_scaleSpin); m_scaleSpin->setValue(std::clamp(c.scale, 0.10f, 4.00f)); }
    if (m_brightness) m_brightness->setValue(std::clamp(int(std::round(c.brightness * 100.0f)), -100, 100));
    if (m_brightnessSpin) { QSignalBlocker b(m_brightnessSpin); m_brightnessSpin->setValue(std::clamp(c.brightness, -1.0f, 1.0f)); }
    if (m_contrast) m_contrast->setValue(std::clamp(int(std::round(c.contrast * 100.0f)), 0, 200));
    if (m_contrastSpin) { QSignalBlocker b(m_contrastSpin); m_contrastSpin->setValue(std::clamp(c.contrast, 0.0f, 2.0f)); }
    if (m_neutral) m_neutral->setValue(std::clamp(int(std::round(c.neutralPoint * 100.0f)), 0, 100));
    if (m_neutralSpin) { QSignalBlocker b(m_neutralSpin); m_neutralSpin->setValue(std::clamp(c.neutralPoint, 0.0f, 1.0f)); }
    if (m_cutoffPolicy) m_cutoffPolicy->setCurrentIndex(std::clamp(int(c.cutoffPolicy), 0, 3));
    if (m_cutoff) m_cutoff->setRange(c.cutoffMin, c.cutoffMax);
    if (m_hOffset) m_hOffset->setValue(c.horizontalOffset);
    if (m_vOffset) m_vOffset->setValue(c.verticalOffset);
    if (m_hRandom) m_hRandom->setChecked(c.randomHorizontalOffset);
    if (m_vRandom) m_vRandom->setChecked(c.randomVerticalOffset);
    if (m_invert) m_invert->setChecked(c.invert);
    if (m_autoInvertEraser) m_autoInvertEraser->setChecked(c.autoInvertForEraser);

    reloadTextures();
    m_loading = false;
}

void BrushTexturePanel::emitChanged()
{
    if (m_loading) return;
    // Pull every control into m_config.
    m_config.texturingMode = static_cast<TextureConfig::TexturingMode>(m_mode->currentIndex());
    m_config.texturing = (m_config.texturingMode == TextureConfig::TexturingMode::Subtract)
                             ? TextureConfig::Texturing::Subtract
                             : TextureConfig::Texturing::Multiply;
    m_config.softTexturing = m_soft->isChecked();
    m_config.scale = float(m_scaleSpin->value());
    m_config.brightness = float(m_brightnessSpin->value());
    m_config.contrast = float(m_contrastSpin->value());
    m_config.neutralPoint = float(m_neutralSpin->value());
    m_config.cutoffPolicy = static_cast<TextureConfig::CutoffPolicy>(m_cutoffPolicy->currentIndex());
    m_config.cutoffMin = m_cutoff->low();
    m_config.cutoffMax = m_cutoff->high();
    m_config.horizontalOffset = m_hOffset->value();
    m_config.verticalOffset = m_vOffset->value();
    m_config.randomHorizontalOffset = m_hRandom->isChecked();
    m_config.randomVerticalOffset = m_vRandom->isChecked();
    m_config.invert = m_invert->isChecked();
    m_config.autoInvertForEraser = m_autoInvertEraser->isChecked();
    emit changed();
}
