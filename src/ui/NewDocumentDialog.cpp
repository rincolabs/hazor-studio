#include "NewDocumentDialog.hpp"
#include "color/ColorProfileRepository.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "ui/IconUtils.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>

#include "colorpicker/ColorPickerDialog.hpp"

namespace {
const char* kCustomPresetsKey = "newDocument/customPresets";

// Map a width/height unit-combo label to the token stored in a CanvasPreset.
QString unitToken(const QString& comboText)
{
    if (comboText.startsWith(QStringLiteral("Inch"), Qt::CaseInsensitive)
        || comboText.startsWith(QStringLiteral("in"), Qt::CaseInsensitive))
        return QStringLiteral("in");
    if (comboText.startsWith(QStringLiteral("cm"), Qt::CaseInsensitive))
        return QStringLiteral("cm");
    if (comboText.startsWith(QStringLiteral("mm"), Qt::CaseInsensitive))
        return QStringLiteral("mm");
    return QStringLiteral("px");
}
} // namespace

NewDocumentDialog::NewDocumentDialog(QWidget* parent)
    : QDialog(parent)
{
    setUpdatesEnabled(false);

    setWindowTitle(tr("New | Edit"));
    setMinimumSize(760, 520);
    resize(820, 560);

    auto* t = ThemeManager::instance()->current();
    if (t) {
        QString qss = t->exportDialogStyleSheet();
        qss += QStringLiteral(
                   "QDoubleSpinBox { min-height: 20px; padding: %1px; }\n"
                   "QSplitter { background: %2; }\n"
                   "QTreeWidget#presetTree { background: %3; }\n")
                   .arg(t->spaceSM)
                   .arg(t->colorSurface.name())
                   .arg(t->colorBackgroundTertiary.name());
        setStyleSheet(qss);
    }

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(t->spaceLG);
    mainLayout->setContentsMargins(t->spaceXL, t->spaceLG, t->spaceXL, t->spaceLG);

    // ── Title ──
    auto* title = new QLabel(tr("<h2>New | Edit</h2>"), this);
    title->setTextFormat(Qt::RichText);
    mainLayout->addWidget(title);

    // ── Body: presets | form ──
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(createPresetPanel());
    splitter->addWidget(createFormSection());
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({220, 500});
    mainLayout->addWidget(splitter, 1);

    // ── Footer ──
    auto* footerLayout = new QHBoxLayout;
    footerLayout->addStretch();
    m_imageSizeLabel = new QLabel(this);
    m_imageSizeLabel->setObjectName("footerLabel");
    footerLayout->addWidget(m_imageSizeLabel);
    mainLayout->addLayout(footerLayout);

    // ── Buttons ──
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    m_cancelBtn->setObjectName("cancelBtn");
    m_cancelBtn->setMinimumHeight(32);
    btnRow->addWidget(m_cancelBtn);
    m_okBtn = new QPushButton(tr("OK"), this);
    m_okBtn->setObjectName("okBtn");
    m_okBtn->setDefault(true);
    m_okBtn->setMinimumHeight(32);
    btnRow->addWidget(m_okBtn);
    mainLayout->addLayout(btnRow);

    loadCustomPresets();
    populatePresetTree();
    updateImageSizeDisplay();

    connect(m_widthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &NewDocumentDialog::onWidthChanged);
    connect(m_heightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &NewDocumentDialog::onHeightChanged);
    connect(m_widthUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NewDocumentDialog::onWidthUnitChanged);
    connect(m_heightUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NewDocumentDialog::onHeightUnitChanged);
    connect(m_resolutionSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &NewDocumentDialog::onResolutionChanged);
    connect(m_resolutionUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NewDocumentDialog::onResolutionUnitChanged);
    connect(m_backgroundCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NewDocumentDialog::onBackgroundChanged);
    connect(m_bgColorBtn, &QPushButton::clicked,
            this, &NewDocumentDialog::pickBackgroundColor);
    connect(m_okBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    setUpdatesEnabled(true);
}

// ── Preset sidebar ───────────────────────────────────────────────────────────

QWidget* NewDocumentDialog::createPresetPanel()
{
    auto* t = ThemeManager::instance()->current();
    auto* panel = new QWidget(this);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(t->spaceSM);

    auto* title = new QLabel(tr("Presets"), this);
    title->setAlignment(Qt::AlignHCenter);
    layout->addWidget(title);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Search"));
    m_searchEdit->setClearButtonEnabled(true);
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &NewDocumentDialog::onSearchChanged);
    layout->addWidget(m_searchEdit);

    m_presetTree = new QTreeWidget(this);
    m_presetTree->setObjectName(QStringLiteral("presetTree"));
    m_presetTree->setHeaderHidden(true);
    m_presetTree->setColumnCount(1);
    m_presetTree->setMinimumWidth(200);
    m_presetTree->setIndentation(10);
    m_presetTree->setIconSize(QSize(24, 24));
    connect(m_presetTree, &QTreeWidget::itemSelectionChanged,
            this, &NewDocumentDialog::onPresetSelectionChanged);
    layout->addWidget(m_presetTree, 1);

    auto* toolRow = new QHBoxLayout;
    m_addPresetBtn = new QPushButton(QStringLiteral("+"), this);
    m_addPresetBtn->setObjectName("savePresetBtn");
    m_addPresetBtn->setFixedWidth(38);
    m_addPresetBtn->setToolTip(tr("Save the current size as a custom preset"));
    connect(m_addPresetBtn, &QPushButton::clicked, this, &NewDocumentDialog::onAddPreset);
    toolRow->addWidget(m_addPresetBtn);
    m_removePresetBtn = new QPushButton(QStringLiteral("−"), this);
    m_removePresetBtn->setObjectName("deletePresetBtn");
    m_removePresetBtn->setFixedWidth(38);
    m_removePresetBtn->setToolTip(tr("Delete the selected custom preset"));
    m_removePresetBtn->setEnabled(false);
    connect(m_removePresetBtn, &QPushButton::clicked, this, &NewDocumentDialog::onRemovePreset);
    toolRow->addWidget(m_removePresetBtn);
    toolRow->addStretch();
    layout->addLayout(toolRow);

    return panel;
}

void NewDocumentDialog::populatePresetTree()
{
    m_presetTree->clear();
    QHash<QString, QTreeWidgetItem*> groups;
    auto ensureGroup = [&](const QString& group) -> QTreeWidgetItem* {
        auto it = groups.constFind(group);
        if (it != groups.constEnd())
            return it.value();
        auto* node = new QTreeWidgetItem(m_presetTree, {group});
        node->setFlags(Qt::ItemIsEnabled);
        groups.insert(group, node);
        return node;
    };

    // A media-type icon makes the unified list clear at a glance (a platform can
    // list both a video size and an image thumbnail).
    const QIcon videoIcon = makeIcon(QStringLiteral(":/icons/play-circle.png"));
    const QIcon imageIcon = makeIcon(QStringLiteral(":/icons/image.png"));
    auto decorate = [&](QTreeWidgetItem* leaf, const canvaspresets::CanvasPreset& p) {
        leaf->setIcon(0, p.kind == canvaspresets::DocumentKind::Animation
                             ? videoIcon : imageIcon);
        leaf->setData(0, Qt::UserRole, canvaspresets::toVariantMap(p));
    };

    for (const canvaspresets::CanvasPreset& p : canvaspresets::builtinPresets()) {
        auto* leaf = new QTreeWidgetItem(ensureGroup(p.group), {p.name});
        decorate(leaf, p);
    }
    if (!m_customPresets.isEmpty()) {
        QTreeWidgetItem* customGroup = ensureGroup(tr("Custom"));
        for (const canvaspresets::CanvasPreset& p : m_customPresets) {
            auto* leaf = new QTreeWidgetItem(customGroup, {p.name});
            decorate(leaf, p);
        }
    }
    m_presetTree->expandAll();
}

void NewDocumentDialog::onPresetSelectionChanged()
{
    auto* item = m_presetTree->currentItem();
    if (!item) {
        m_removePresetBtn->setEnabled(false);
        return;
    }
    const QVariant data = item->data(0, Qt::UserRole);
    if (!data.isValid()) {
        m_removePresetBtn->setEnabled(false);
        return;
    }
    const canvaspresets::CanvasPreset preset =
        canvaspresets::fromVariantMap(data.toMap());
    m_removePresetBtn->setEnabled(!preset.builtin);
    if (!m_updating)
        applyPreset(preset);
}

void NewDocumentDialog::applyPreset(const canvaspresets::CanvasPreset& preset)
{
    m_updating = true;

    m_widthSpin->setValue(preset.width);
    m_heightSpin->setValue(preset.height);

    const int unitIdx = m_widthUnitCombo->findText(preset.unit, Qt::MatchContains);
    if (unitIdx >= 0) {
        m_widthUnitCombo->setCurrentIndex(unitIdx);
        m_heightUnitCombo->setCurrentIndex(unitIdx);
    }

    m_resolutionSpin->setValue(preset.resolution);
    m_resolutionUnitCombo->setCurrentIndex(0); // Pixels/Inch

    const QString kindLabel = canvaspresets::documentKindLabel(preset.kind);
    const int dtIdx = m_docTypeCombo->findText(kindLabel);
    if (dtIdx >= 0)
        m_docTypeCombo->setCurrentIndex(dtIdx);

    m_updating = false;
    updateImageSizeDisplay();
}

void NewDocumentDialog::onSearchChanged(const QString& text)
{
    const QString needle = text.trimmed();
    for (int i = 0; i < m_presetTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* group = m_presetTree->topLevelItem(i);
        int visibleChildren = 0;
        for (int j = 0; j < group->childCount(); ++j) {
            QTreeWidgetItem* leaf = group->child(j);
            const bool match = needle.isEmpty()
                || leaf->text(0).contains(needle, Qt::CaseInsensitive);
            leaf->setHidden(!match);
            if (match)
                ++visibleChildren;
        }
        group->setHidden(group->childCount() > 0 && visibleChildren == 0);
    }
}

void NewDocumentDialog::onAddPreset()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Save Preset"),
        tr("Preset name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    canvaspresets::CanvasPreset p;
    p.name = name.trimmed();
    p.group = tr("Custom");
    p.builtin = false;
    p.kind = canvaspresets::documentKindFromLabel(m_docTypeCombo->currentText());
    p.width = m_widthSpin->value();
    p.height = m_heightSpin->value();
    p.unit = unitToken(m_widthUnitCombo->currentText());
    p.resolution = m_resolutionSpin->value();

    for (auto& existing : m_customPresets) {
        if (existing.name == p.name) {
            existing = p;
            saveCustomPresets();
            populatePresetTree();
            return;
        }
    }
    m_customPresets.push_back(p);
    saveCustomPresets();
    populatePresetTree();
}

void NewDocumentDialog::onRemovePreset()
{
    auto* item = m_presetTree->currentItem();
    if (!item)
        return;
    const QVariant data = item->data(0, Qt::UserRole);
    if (!data.isValid())
        return;
    const canvaspresets::CanvasPreset preset =
        canvaspresets::fromVariantMap(data.toMap());
    if (preset.builtin)
        return;
    for (int i = 0; i < m_customPresets.size(); ++i) {
        if (m_customPresets[i].name == preset.name) {
            m_customPresets.remove(i);
            break;
        }
    }
    saveCustomPresets();
    populatePresetTree();
}

void NewDocumentDialog::loadCustomPresets()
{
    m_customPresets.clear();
    QSettings s;
    const QJsonDocument doc = QJsonDocument::fromJson(
        s.value(kCustomPresetsKey).toString().toUtf8());
    for (const QJsonValue& v : doc.array())
        m_customPresets.push_back(
            canvaspresets::fromVariantMap(v.toObject().toVariantMap()));
}

void NewDocumentDialog::saveCustomPresets()
{
    QJsonArray arr;
    for (const canvaspresets::CanvasPreset& p : m_customPresets)
        arr.append(QJsonObject::fromVariantMap(canvaspresets::toVariantMap(p)));
    QSettings s;
    s.setValue(kCustomPresetsKey,
               QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

// ── Form ─────────────────────────────────────────────────────────────────────

QWidget* NewDocumentDialog::createFormSection()
{
    auto* t = ThemeManager::instance()->current();
    auto* container = new QWidget(this);
    auto* form = new QFormLayout(container);
    form->setLabelAlignment(Qt::AlignRight);
    form->setHorizontalSpacing(t->spaceLG);
    form->setVerticalSpacing(t->spaceMD);

    auto addField = [&](const QString& label, QWidget* field) {
        auto* lbl = new QLabel(label + ":", this);
        lbl->setFixedWidth(120);
        form->addRow(lbl, field);
    };

    // ── Name ──
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setText(tr("My new document"));
    m_nameEdit->setMinimumWidth(200);
    addField(tr("Name"), m_nameEdit);

    // ── Document Type ── (Photo | Animation; later selects the workspace)
    m_docTypeCombo = new QComboBox(this);
    m_docTypeCombo->addItems({tr("Photo"), tr("Animation")});
    addField(tr("Document Type"), m_docTypeCombo);

    // ── Dimensions section ──
    auto* dimFrame = new QFrame(this);
    dimFrame->setObjectName("dimFrame");
    dimFrame->setFrameShape(QFrame::StyledPanel);
    auto* dimGrid = new QGridLayout(dimFrame);
    dimGrid->setSpacing(t->spaceSM);
    dimGrid->setContentsMargins(t->spaceMD, t->spaceSM, t->spaceMD, t->spaceSM);

    auto* widthLbl = new QLabel(tr("Width:"), this);
    m_widthSpin = new QDoubleSpinBox(this);
    m_widthSpin->setRange(0.01, 99999.0);
    m_widthSpin->setDecimals(2);
    m_widthSpin->setValue(10.0);
    m_widthSpin->setFixedWidth(90);
    m_widthUnitCombo = new QComboBox(this);
    m_widthUnitCombo->addItems({tr("Inches"), tr("cm"), tr("mm"), tr("px")});

    auto* heightLbl = new QLabel(tr("Height:"), this);
    m_heightSpin = new QDoubleSpinBox(this);
    m_heightSpin->setRange(0.01, 99999.0);
    m_heightSpin->setDecimals(2);
    m_heightSpin->setValue(8.0);
    m_heightSpin->setFixedWidth(90);
    m_heightUnitCombo = new QComboBox(this);
    m_heightUnitCombo->addItems({tr("Inches"), tr("cm"), tr("mm"), tr("px")});

    auto* resLbl = new QLabel(tr("Resolution:"), this);
    m_resolutionSpin = new QSpinBox(this);
    m_resolutionSpin->setRange(1, 9999);
    m_resolutionSpin->setValue(300);
    m_resolutionSpin->setFixedWidth(90);
    m_resolutionUnitCombo = new QComboBox(this);
    m_resolutionUnitCombo->addItems({tr("Pixels/Inch"), tr("Pixels/cm")});

    dimGrid->addWidget(widthLbl, 0, 0);
    dimGrid->addWidget(m_widthSpin, 0, 1);
    dimGrid->addWidget(m_widthUnitCombo, 0, 2);
    dimGrid->addWidget(heightLbl, 1, 0);
    dimGrid->addWidget(m_heightSpin, 1, 1);
    dimGrid->addWidget(m_heightUnitCombo, 1, 2);
    dimGrid->addWidget(resLbl, 2, 0);
    dimGrid->addWidget(m_resolutionSpin, 2, 1);
    dimGrid->addWidget(m_resolutionUnitCombo, 2, 2);

    // wrap in a vertical layout so the frame fills width
    auto* dimWrapper = new QWidget(this);
    auto* dimWrapperLayout = new QVBoxLayout(dimWrapper);
    dimWrapperLayout->setContentsMargins(0, 0, 0, 0);
    dimWrapperLayout->addWidget(dimFrame);
    form->addRow(dimWrapper);

    // ── Color Mode ──
    m_colorModeCombo = new QComboBox(this);
    m_colorModeCombo->addItems({
        tr("RGB Color, 8 bit"),
        tr("RGB Color, 16 bit"),
        tr("CMYK Color, 8 bit"),
        tr("CMYK Color, 16 bit"),
        tr("Grayscale, 8 bit"),
        tr("Grayscale, 16 bit"),
    });
    addField(tr("Color Mode"), m_colorModeCombo);

    // ── Background Contents ──
    auto* bgWidget = new QWidget(this);
    auto* bgLayout = new QHBoxLayout(bgWidget);
    bgLayout->setContentsMargins(0, 0, 0, 0);
    bgLayout->setSpacing(t->spaceSM);

    m_backgroundCombo = new QComboBox(this);
    m_backgroundCombo->addItems({
        tr("White"),
        tr("Black"),
        tr("Transparent"),
        tr("Background Color"),
        tr("Custom"),
    });
    m_backgroundCombo->setMinimumWidth(140);
    bgLayout->addWidget(m_backgroundCombo);

    m_bgColorBtn = new QPushButton(this);
    m_bgColorBtn->setObjectName("bgColorBtn");
    m_bgColorBtn->setFixedSize(24, 24);
    m_bgColorBtn->setCursor(Qt::PointingHandCursor);
    m_bgColorBtn->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid %2; border-radius: %3px;").arg(QColor(Qt::white).name()).arg(t->colorBorder.name()).arg(t->radiusSM));
    bgLayout->addWidget(m_bgColorBtn);
    bgLayout->addStretch();

    addField(tr("Background Contents"), bgWidget);

    // ── Advanced Section ──
    auto* advGroup = new QGroupBox(tr("Advanced"), this);
    advGroup->setFlat(false);
    auto* advForm = new QFormLayout(advGroup);
    advForm->setHorizontalSpacing(10);

    m_colorProfileCombo = new QComboBox(this);
    {
        ColorProfileRepository repository;
        for (const ColorProfile& profile : repository.builtInRgbProfiles())
            m_colorProfileCombo->addItem(profile.displayName(),
                                         static_cast<int>(profile.kind()));
        // -1 sentinel = untagged (skip colour management for this document).
        m_colorProfileCombo->addItem(tr("Don't Color Manage this Document"), -1);
        const int sIdx = m_colorProfileCombo->findData(
            static_cast<int>(ColorProfileKind::SRgb));
        if (sIdx >= 0)
            m_colorProfileCombo->setCurrentIndex(sIdx);
    }
    advForm->addRow(tr("Color Profile:"), m_colorProfileCombo);

    auto* aspectLbl = new QLabel(tr("Square Pixels"), this);
    aspectLbl->setObjectName("infoLabel");
    advForm->addRow(tr("Pixel Aspect Ratio:"), aspectLbl);

    form->addRow(advGroup);

    return container;
}

void NewDocumentDialog::setSettings(const DocumentSettings& s)
{
    m_nameEdit->setText(s.name);

    int dtIdx = m_docTypeCombo->findText(s.documentType);
    if (dtIdx >= 0) m_docTypeCombo->setCurrentIndex(dtIdx);

    m_widthSpin->setValue(s.width);
    m_heightSpin->setValue(s.height);

    int wuIdx = m_widthUnitCombo->findText(s.unit, Qt::MatchContains);
    if (wuIdx >= 0) m_widthUnitCombo->setCurrentIndex(wuIdx);
    int huIdx = m_heightUnitCombo->findText(s.unit, Qt::MatchContains);
    if (huIdx >= 0) m_heightUnitCombo->setCurrentIndex(huIdx);

    m_resolutionSpin->setValue(s.resolution);

    int ruIdx = m_resolutionUnitCombo->findText(s.resolutionUnit, Qt::MatchContains);
    if (ruIdx >= 0) m_resolutionUnitCombo->setCurrentIndex(ruIdx);

    int cmIdx = m_colorModeCombo->findText(s.colorMode, Qt::MatchContains);
    if (cmIdx >= 0) m_colorModeCombo->setCurrentIndex(cmIdx);

    if (m_colorProfileCombo) {
        const int want = s.colorManaged ? static_cast<int>(s.colorProfileKind) : -1;
        const int cpIdx = m_colorProfileCombo->findData(want);
        m_colorProfileCombo->setCurrentIndex(cpIdx >= 0 ? cpIdx : 0);
    }

    int bgIdx = m_backgroundCombo->findText(s.background, Qt::MatchContains);
    if (bgIdx >= 0) m_backgroundCombo->setCurrentIndex(bgIdx);

    auto* t = ThemeManager::instance()->current();
    m_bgColor = s.backgroundColor;
    m_bgColorBtn->setStyleSheet(
        QStringLiteral("background-color: %1; border: 1px solid %2; border-radius: %3px;").arg(m_bgColor.name()).arg(t->colorBorder.name()).arg(t->radiusSM));

    updateImageSizeDisplay();
}

DocumentSettings NewDocumentDialog::settings() const
{
    DocumentSettings s;
    s.name = m_nameEdit->text();
    s.documentType = m_docTypeCombo->currentText();
    s.width = m_widthSpin->value();
    s.height = m_heightSpin->value();
    s.unit = m_widthUnitCombo->currentText();
    s.resolution = m_resolutionSpin->value();
    s.resolutionUnit = m_resolutionUnitCombo->currentText();
    s.colorMode = m_colorModeCombo->currentText();
    s.background = m_backgroundCombo->currentText();
    s.backgroundColor = m_bgColor;
    if (m_colorProfileCombo) {
        const int kindData = m_colorProfileCombo->currentData().toInt();
        s.colorManaged = (kindData >= 0);
        s.colorProfileKind = s.colorManaged
            ? static_cast<ColorProfileKind>(kindData)
            : ColorProfileKind::SRgb;
        s.colorProfile = m_colorProfileCombo->currentText();
    }
    s.pixelAspect = "Square Pixels";
    return s;
}

// ── Conversion helpers ──

double NewDocumentDialog::valueToInches(double v, const QString& unit) const
{
    if (unit.startsWith("Inch") || unit.startsWith("in"))
        return v;
    if (unit.startsWith("cm"))
        return v / 2.54;
    if (unit.startsWith("mm"))
        return v / 25.4;
    if (unit.startsWith("px"))
        return v / m_resolutionSpin->value();
    return v;
}

// ── Slots ──

void NewDocumentDialog::onWidthChanged(double)
{
    if (m_updating) return;
    updateImageSizeDisplay();
}

void NewDocumentDialog::onHeightChanged(double)
{
    if (m_updating) return;
    updateImageSizeDisplay();
}

void NewDocumentDialog::onWidthUnitChanged(int)
{
    if (m_updating) return;
    updateImageSizeDisplay();
}

void NewDocumentDialog::onHeightUnitChanged(int)
{
    if (m_updating) return;
    updateImageSizeDisplay();
}

void NewDocumentDialog::onResolutionChanged(int)
{
    if (m_updating) return;
    updateImageSizeDisplay();
}

void NewDocumentDialog::onResolutionUnitChanged(int)
{
    if (m_updating) return;
    updateImageSizeDisplay();
}

void NewDocumentDialog::onBackgroundChanged(int index)
{
    if (index == 4) { // "Custom"
        pickBackgroundColor();
    }
}

void NewDocumentDialog::pickBackgroundColor()
{
    auto* dlg = new ColorPickerDialog(m_bgColor, ColorPickerMode::Foreground, this);
    connect(dlg, &ColorPickerDialog::colorAccepted, this, [this](const QColor& c) {
        auto* t = ThemeManager::instance()->current();
        m_bgColor = c;
        m_bgColorBtn->setStyleSheet(
            QStringLiteral("background-color: %1; border: 1px solid %2; border-radius: %3px;").arg(m_bgColor.name()).arg(t->colorBorder.name()).arg(t->radiusSM));
    });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->open();
}

void NewDocumentDialog::updateImageSizeDisplay()
{
    double w = m_widthSpin->value();
    double h = m_heightSpin->value();
    QString wUnit = m_widthUnitCombo->currentText();
    QString hUnit = m_heightUnitCombo->currentText();
    int res = m_resolutionSpin->value();
    QString resUnit = m_resolutionUnitCombo->currentText();

    double wInch = valueToInches(w, wUnit);
    double hInch = valueToInches(h, hUnit);

    double ppi = (resUnit.startsWith("Pixels/cm")) ? res * 2.54 : res;
    double wPx = wInch * ppi;
    double hPx = hInch * ppi;

    double bytesPerPixel = 4.0;
    double sizeBytes = wPx * hPx * bytesPerPixel;
    double sizeMB = sizeBytes / (1024.0 * 1024.0);

    m_imageSizeLabel->setText(
        tr("Image Size: %1M  (%2 x %3 px)")
            .arg(sizeMB, 0, 'f', 1)
            .arg(static_cast<int>(std::round(wPx)))
            .arg(static_cast<int>(std::round(hPx))));
}
