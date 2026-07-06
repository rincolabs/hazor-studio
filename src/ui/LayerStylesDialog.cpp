#include "LayerStylesDialog.hpp"
#include "ui/AppCheckBox.hpp"

#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include "colorpicker/ColorPickerDialog.hpp"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

LayerStylesDialog::LayerStylesDialog(const std::vector<LayerEffect>& effects,
                                     QWidget* parent)
    : QDialog(parent)
{
    m_defs = {
        {QStringLiteral("drop_shadow"), QStringLiteral("Drop Shadow")},
        {QStringLiteral("inner_shadow"), QStringLiteral("Inner Shadow")},
        {QStringLiteral("stroke"), QStringLiteral("Stroke")},
        {QStringLiteral("color_overlay"), QStringLiteral("Color Overlay")},
        {QStringLiteral("gradient_overlay"), QStringLiteral("Gradient Overlay")},
        {QStringLiteral("outer_glow"), QStringLiteral("Outer Glow")},
        {QStringLiteral("inner_glow"), QStringLiteral("Inner Glow")},
    };

    for (const auto& def : m_defs) {
        const QVariantMap defaults = LayerEffect::defaultStyleParams(def.type);
        LayerEffect effect(def.type, defaults, defaults);
        effect.enabled = false;
        for (const auto& existing : effects) {
            if (existing.type == def.type) {
                effect = existing;
                break;
            }
        }
        if (effect.params.isEmpty())
            effect.params = defaults;
        if (effect.defaultParams.isEmpty())
            effect.defaultParams = defaults;
        m_effects.push_back(effect);
    }

    buildUi();
}

std::vector<LayerEffect> LayerStylesDialog::styles() const
{
    std::vector<LayerEffect> out;
    out.reserve(m_effects.size());
    for (const auto& effect : m_effects)
        out.push_back(effect);
    return out;
}

void LayerStylesDialog::buildUi()
{
    auto* t = ThemeManager::instance()->current();
    setWindowTitle(tr("Layer Styles"));
    resize(600, 350);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(t->spaceMD, t->spaceMD, t->spaceMD, t->spaceMD);
    root->setSpacing(t->spaceXL);

    auto* body = new QHBoxLayout();
    body->setSpacing(t->spaceMD);
    root->addLayout(body, 1);

    const int effectRowPadding = 3;
    const int effectRowMargin = 1;

    m_list = new QListWidget(this);
    m_list->setFixedWidth(210);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setStyleSheet(QStringLiteral(
        "QListWidget { background: %1; border: none; }"
        "QListWidget::item { background: transparent; border: none; margin: 0px; }"
    )
    .arg(t->colorBackgroundTertiary.name())
    .arg(t->spaceMD)
    .arg(t->colorSurfacePressed.name(), t->colorSurface.name())
    );

    for (int i = 0; i < m_defs.size(); ++i) {
        auto* item = new QListWidgetItem(m_list);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        auto* rowContainer = new QWidget(m_list);
        auto* rowContainerLayout = new QVBoxLayout(rowContainer);
        rowContainerLayout->setContentsMargins(effectRowMargin, effectRowMargin,
                                               effectRowMargin, effectRowMargin);
        rowContainerLayout->setSpacing(0);

        auto* rowHost = new QWidget(rowContainer);
        auto* rowLayout = new QHBoxLayout(rowHost);
        rowLayout->setContentsMargins(t->spaceSM, t->spaceXS, t->spaceSM, t->spaceXS);
        rowLayout->setSpacing(2);
        rowLayout->setAlignment(Qt::AlignVCenter);

        auto* check = new AppCheckBox(m_defs[i].label, rowHost);
        check->setChecked(m_effects[i].enabled);
        check->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        const QSize checkSize = check->sizeHint();
        check->setMinimumHeight(checkSize.height());
        rowLayout->addWidget(check);

        const QMargins rowMargins = rowLayout->contentsMargins();
        const int rowHeight = checkSize.height()
                              + rowMargins.top()
                              + rowMargins.bottom()
                              + (effectRowPadding * 2);
        rowHost->setMinimumHeight(rowHeight);
        rowHost->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        rowContainerLayout->addWidget(rowHost);
        item->setSizeHint(QSize(std::max(0, 210 - (t->spaceMD * 2)),
                                rowHeight + (effectRowMargin * 2)));
        m_list->setItemWidget(item, rowContainer);
        m_effectRowWidgets.push_back(rowHost);
        m_effectChecks.push_back(check);

        connect(check, &QCheckBox::clicked, this, [this, i]() {
            if (m_list)
                m_list->setCurrentRow(i);
        });
        connect(check, &QCheckBox::toggled, this, [this, i](bool checked) {
            if (m_updating) return;
            if (i < 0 || i >= m_effects.size()) return;
            m_effects[i].enabled = checked;
            if (m_list)
                m_list->setCurrentRow(i);
            updateEffectRowStyles();
            emitPreview();
        });
    }
    body->addWidget(m_list);

    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea { background: %1; }"
    ).arg(t->colorSurfacePressed.name()));

    m_propertyHost = new QWidget(scroll);
    m_propertyHost->setStyleSheet(QStringLiteral(
        "background: %1;"
    ).arg(t->colorSurfacePressed.name()));
    m_propertyLayout = new QVBoxLayout(m_propertyHost);
    m_propertyLayout->setContentsMargins(t->spaceMD, t->spaceSM, t->spaceMD, t->spaceSM);
    m_propertyLayout->setSpacing(t->spaceSM);
    scroll->setWidget(m_propertyHost);
    body->addWidget(scroll, 1);

    connect(m_list, &QListWidget::currentRowChanged,
            this, [this](int row) {
        updateEffectRowStyles();
        rebuildProperties(row);
    });

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_list->setCurrentRow(0);
    updateEffectRowStyles();
}

void LayerStylesDialog::rebuildProperties(int row)
{
    while (auto* item = m_propertyLayout->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }
    if (row < 0 || row >= m_effects.size()) return;

    auto* title = new QLabel(m_defs[row].label, m_propertyHost);
    auto font = title->font();
    font.setBold(true);
    font.setPixelSize(font.pixelSize() > 0 ? font.pixelSize() + 2 : 14);
    title->setFont(font);
    m_propertyLayout->addWidget(title);

    auto* formHost = new QWidget(m_propertyHost);
    formHost->setStyleSheet(QStringLiteral("background: %1;").arg(
        ThemeManager::instance()->current()->colorSurfacePressed.name()));
    auto* form = new QFormLayout(formHost);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_propertyLayout->addWidget(formHost);

    const QString type = m_defs[row].type;
    if (type == QLatin1String("drop_shadow")) {
        addBlendRow(form, row);
        addColorRow(form, row, tr("Color"), QStringLiteral("color"));
        addDoubleRow(form, row, tr("Opacity"), QStringLiteral("opacity"), 0.0, 1.0);
        addDoubleRow(form, row, tr("Angle"), QStringLiteral("angle"), -180.0, 180.0, 1.0, 0);
        addIntRow(form, row, tr("Distance"), QStringLiteral("distance"), 0, 200);
        addIntRow(form, row, tr("Spread"), QStringLiteral("spread"), 0, 100);
        addDoubleRow(form, row, tr("Blur"), QStringLiteral("blur"), 0.0, 200.0, 1.0, 0);
    } else if (type == QLatin1String("inner_shadow")) {
        addBlendRow(form, row);
        addColorRow(form, row, tr("Color"), QStringLiteral("color"));
        addDoubleRow(form, row, tr("Opacity"), QStringLiteral("opacity"), 0.0, 1.0);
        addDoubleRow(form, row, tr("Angle"), QStringLiteral("angle"), -180.0, 180.0, 1.0, 0);
        addIntRow(form, row, tr("Distance"), QStringLiteral("distance"), 0, 200);
        addDoubleRow(form, row, tr("Blur"), QStringLiteral("blur"), 0.0, 200.0, 1.0, 0);
    } else if (type == QLatin1String("stroke")) {
        addBlendRow(form, row);
        addColorRow(form, row, tr("Color"), QStringLiteral("color"));
        addIntRow(form, row, tr("Size"), QStringLiteral("size"), 0, 200);
        addDoubleRow(form, row, tr("Opacity"), QStringLiteral("opacity"), 0.0, 1.0);
        addPositionRow(form, row);
    } else if (type == QLatin1String("color_overlay")) {
        addBlendRow(form, row);
        addColorRow(form, row, tr("Color"), QStringLiteral("color"));
        addDoubleRow(form, row, tr("Opacity"), QStringLiteral("opacity"), 0.0, 1.0);
    } else if (type == QLatin1String("gradient_overlay")) {
        addBlendRow(form, row);
        addColorRow(form, row, tr("Start"), QStringLiteral("startColor"));
        addColorRow(form, row, tr("End"), QStringLiteral("endColor"));
        addDoubleRow(form, row, tr("Opacity"), QStringLiteral("opacity"), 0.0, 1.0);
        addDoubleRow(form, row, tr("Angle"), QStringLiteral("angle"), -180.0, 180.0, 1.0, 0);
        addDoubleRow(form, row, tr("Scale"), QStringLiteral("scale"), 0.1, 4.0, 0.1, 1);
    } else if (type == QLatin1String("outer_glow") || type == QLatin1String("inner_glow")) {
        addBlendRow(form, row);
        addColorRow(form, row, tr("Color"), QStringLiteral("color"));
        addDoubleRow(form, row, tr("Opacity"), QStringLiteral("opacity"), 0.0, 1.0);
        if (type == QLatin1String("outer_glow"))
            addIntRow(form, row, tr("Spread"), QStringLiteral("spread"), 0, 100);
        addDoubleRow(form, row, tr("Blur"), QStringLiteral("blur"), 0.0, 200.0, 1.0, 0);
    }

    m_propertyLayout->addStretch();
}

void LayerStylesDialog::updateEffectRowStyles()
{
    auto* t = ThemeManager::instance()->current();
    if (!t)
        return;

    const int currentRow = m_list ? m_list->currentRow() : -1;
    for (int i = 0; i < m_effectRowWidgets.size(); ++i) {
        auto* rowHost = m_effectRowWidgets[i];
        auto* check = (i < m_effectChecks.size()) ? m_effectChecks[i] : nullptr;
        if (!rowHost || !check)
            continue;

        const bool selected = (i == currentRow);
        const bool checked = check->isChecked();
        const QColor bg = checked ? t->colorSurfacePressed : t->colorSurface;
        const QColor border = selected ? t->colorAccent : t->colorSurfacePressed;
        rowHost->setStyleSheet(QStringLiteral(
            "background: %1; border: 1px solid %2;")
            .arg(bg.name(), border.name()));

        check->colors.textColor = selected ? QColor(Qt::white) : QColor();
        check->refreshStyle();
    }
}

void LayerStylesDialog::emitPreview()
{
    emit stylesChanged();
}

int LayerStylesDialog::indexForType(const QString& type) const
{
    for (int i = 0; i < m_effects.size(); ++i) {
        if (m_effects[i].type == type)
            return i;
    }
    return -1;
}

LayerEffect& LayerStylesDialog::effectAt(int row)
{
    return m_effects[row];
}

const LayerEffect& LayerStylesDialog::effectAt(int row) const
{
    return m_effects[row];
}

void LayerStylesDialog::addColorRow(QFormLayout* form, int row,
                                    const QString& label, const QString& key)
{
    QColor color = effectAt(row).params.value(key).value<QColor>();
    if (!color.isValid())
        color = QColor(effectAt(row).params.value(key).toString());
    auto* btn = new QPushButton(this);
    btn->setFixedWidth(46);
    auto updateButton = [this, btn](const QColor& c) {
        auto* t = ThemeManager::instance()->current();
        btn->setText(QString());
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { background:%1; color:%2; border:1px solid %3; padding:%4px; }")
            .arg(c.name(QColor::HexRgb),
                 c.lightness() < 128 ? t->colorTextInverted.name() : t->colorTextPrimary.name(),
                 t->colorBorder.name())
            .arg(t->spaceSM));
    };
    updateButton(color);
    connect(btn, &QPushButton::clicked, this, [this, row, key, updateButton]() {
        QColor current = effectAt(row).params.value(key).value<QColor>();
        auto* dlg = new ColorPickerDialog(current, ColorPickerMode::Foreground, this);
        connect(dlg, &ColorPickerDialog::colorAccepted, this,
            [this, row, key, updateButton](const QColor& picked) {
                effectAt(row).params[key] = picked;
                updateButton(picked);
                emitPreview();
            });
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->open();
    });
    form->addRow(label, btn);
}

namespace {
QString suffixForKey(const QString& key)
{
    if (key == QLatin1String("spread"))
        return QStringLiteral("%");
    if (key == QLatin1String("distance")
        || key == QLatin1String("blur")
        || key == QLatin1String("size"))
        return QStringLiteral("px");
    return {};
}
}

void LayerStylesDialog::addIntRow(QFormLayout* form, int row, const QString& label,
                                   const QString& key, int min, int max, int step)
{
    const QString suffix = suffixForKey(key);
    auto* t =ThemeManager::instance()->current();
    auto* container = new QWidget(this);
    container->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* hlay = new QHBoxLayout(container);
    hlay->setContentsMargins(0, 0, 0, 0);

    auto* slider = new QSlider(Qt::Horizontal);
    slider->setRange(min, max);
    slider->setSingleStep(step);
    slider->setValue(effectAt(row).params.value(key, min).toInt());

    auto* spin = new QSpinBox();
    spin->setRange(min, max);
    spin->setSingleStep(step);
    spin->setValue(slider->value());
    spin->setFixedWidth(60);

    // 70% slider width (stretch 7 vs 3 for spinbox+suffix)
    hlay->addWidget(slider, 7);
    hlay->addWidget(spin, 0);
    if (!suffix.isEmpty()) {
        auto* suffixLbl = new QLabel(suffix);
        suffixLbl->setFixedWidth(16);
        hlay->addWidget(suffixLbl, 0);
    }

    connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), slider, &QSlider::setValue);
    connect(slider, &QSlider::valueChanged, this, [this, row, key](int value) {
        effectAt(row).params[key] = value;
        emitPreview();
    });

    form->addRow(label, container);
}

void LayerStylesDialog::addDoubleRow(QFormLayout* form, int row, const QString& label,
                                      const QString& key, double min, double max,
                                      double step, int decimals)
{
    Q_UNUSED(decimals)
    int factor = 1;
    QString suffix;
    if (key == QLatin1String("opacity")) {
        factor = 100;
        suffix = QStringLiteral("%");
    } else if (key == QLatin1String("scale")) {
        factor = 100;
        suffix = QStringLiteral("%");
    } else if (key == QLatin1String("angle")) {
        factor = 1;
        suffix = QStringLiteral("\u00B0");
    } else {
        factor = 1;
        suffix = suffixForKey(key);
    }

    const int sliderMin = static_cast<int>(std::round(min * factor));
    const int sliderMax = static_cast<int>(std::round(max * factor));
    const int sliderStep = std::max(1, static_cast<int>(std::round(step * factor)));
    const int sliderValue = static_cast<int>(
        std::round(effectAt(row).params.value(key, min).toDouble() * factor));

    auto* container = new QWidget(this);
    container->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* hlay = new QHBoxLayout(container);
    hlay->setContentsMargins(0, 0, 0, 0);

    auto* slider = new QSlider(Qt::Horizontal);
    slider->setRange(sliderMin, sliderMax);
    slider->setSingleStep(sliderStep);
    slider->setValue(sliderValue);

    auto* spin = new QSpinBox();
    spin->setRange(sliderMin, sliderMax);
    spin->setSingleStep(sliderStep);
    spin->setValue(sliderValue);
    spin->setFixedWidth(60);

    hlay->addWidget(slider, 7);
    hlay->addWidget(spin, 0);
    if (!suffix.isEmpty()) {
        auto* suffixLbl = new QLabel(suffix);
        suffixLbl->setFixedWidth(16);
        hlay->addWidget(suffixLbl, 0);
    }

    connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), slider, &QSlider::setValue);
    connect(slider, &QSlider::valueChanged, this, [this, row, key, factor](int value) {
        effectAt(row).params[key] = static_cast<double>(value) / factor;
        emitPreview();
    });

    form->addRow(label, container);
}

void LayerStylesDialog::addBlendRow(QFormLayout* form, int row)
{
    auto* combo = new QComboBox(this);
    const QVector<QPair<QString, QString>> modes = {
        {QStringLiteral("normal"), QStringLiteral("Normal")},
        {QStringLiteral("multiply"), QStringLiteral("Multiply")},
        {QStringLiteral("screen"), QStringLiteral("Screen")},
        {QStringLiteral("overlay"), QStringLiteral("Overlay")},
        {QStringLiteral("darken"), QStringLiteral("Darken")},
        {QStringLiteral("lighten"), QStringLiteral("Lighten")},
    };
    const QString current = effectAt(row).params.value(QStringLiteral("blendMode"), QStringLiteral("normal")).toString();
    for (const auto& mode : modes)
        combo->addItem(mode.second, mode.first);
    combo->setCurrentIndex(std::max(0, combo->findData(current)));
    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, row, combo](int) {
        effectAt(row).params[QStringLiteral("blendMode")] = combo->currentData().toString();
        emitPreview();
    });
    form->addRow(tr("Blend Mode"), combo);
}

void LayerStylesDialog::addPositionRow(QFormLayout* form, int row)
{
    auto* combo = new QComboBox(this);
    combo->addItem(tr("Outside"), QStringLiteral("outside"));
    combo->addItem(tr("Center"), QStringLiteral("center"));
    combo->addItem(tr("Inside"), QStringLiteral("inside"));
    const QString current = effectAt(row).params.value(QStringLiteral("position"), QStringLiteral("outside")).toString();
    combo->setCurrentIndex(std::max(0, combo->findData(current)));
    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, row, combo](int) {
        effectAt(row).params[QStringLiteral("position")] = combo->currentData().toString();
        emitPreview();
    });
    form->addRow(tr("Position"), combo);
}
