#include "PropertiesPanel.hpp"
#include "controller/ImageController.hpp"
#include "ui/AlignBar.hpp"
#include "ui/AppCheckBox.hpp"
#include "ui/AppComboBox.hpp"
#include "ui/ColorBalanceAdjustmentWidget.hpp"
#include "ui/CurvesEditorWidget.hpp"
#include "ui/HueSaturationAdjustmentWidget.hpp"
#include "ui/SolidColorAdjustmentWidget.hpp"
#include "ui/ScrubbableValueInput.h"
#include "ui/TransformFieldsWidget.hpp"
#include "core/GuideTypes.hpp"
#include "IconUtils.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QAbstractSpinBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QScrollArea>
#include <QSlider>
#include <QPushButton>
#include <QToolButton>
#include <QComboBox>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QPixmap>

#include <utility>

// ── CollapsibleSection ───────────────────────────────────────────────────────
// Lightweight collapsible section: a clickable header with a disclosure
// arrow that collapses/expands its body. Kept local to the panel — it carries no
// app logic, only layout, and reuses the theme tokens for spacing/colours.
class CollapsibleSection : public QWidget {
public:
    explicit CollapsibleSection(const QString& title, QWidget* parent = nullptr)
        : QWidget(parent)
    {
        auto* t = ThemeManager::instance()->current();
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        m_header = new QToolButton(this);
        m_header->setText(title);
        m_header->setCheckable(true);
        m_header->setChecked(true);
        m_header->setArrowType(Qt::DownArrow);
        m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_header->setAutoRaise(true);
        m_header->setCursor(Qt::PointingHandCursor);
        m_header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_header->setStyleSheet(QStringLiteral(
            "QToolButton { border: none; padding: %1px %2px; color: %3; "
            "font-weight: bold; text-align: left; background: transparent; }"
            "QToolButton:hover { color: %4; }")
            .arg(t->spaceSM).arg(t->spaceMD)
            .arg(t->colorTextSecondary.name())
            .arg(t->colorTextBright.name()));
        root->addWidget(m_header);

        m_body = new QWidget(this);
        m_bodyLay = new QVBoxLayout(m_body);
        m_bodyLay->setContentsMargins(t->spaceMD, t->spaceXS, t->spaceMD, t->spaceMD);
        m_bodyLay->setSpacing(t->spaceSM);
        root->addWidget(m_body);

        connect(m_header, &QToolButton::toggled, this, [this](bool on) {
            m_body->setVisible(on);
            m_header->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        });
    }

    QVBoxLayout* body() { return m_bodyLay; }

private:
    QToolButton* m_header = nullptr;
    QWidget* m_body = nullptr;
    QVBoxLayout* m_bodyLay = nullptr;
};

// Builds a 28×24 icon button matching the Align bar's affordances.
static QPushButton* makeIconButton(const QString& iconName, const QString& tooltip,
                                   QWidget* parent)
{
    auto* btn = new QPushButton(parent);
    btn->setIcon(makeIcon(QStringLiteral(":/icons/%1.png").arg(iconName)));
    btn->setIconSize(QSize(20, 20));
    btn->setFixedSize(28, 24);
    btn->setToolTip(tooltip);
    return btn;
}

// Builds a checkable icon tool-button whose :checked state reads as "active"
// (accent border + selected surface), used by the Document page's orientation
// buttons and ruler toggle.
static QToolButton* makeToggleButton(const QString& iconName, const QString& tooltip,
                                     QWidget* parent)
{
    auto* t = ThemeManager::instance()->current();
    auto* btn = new QToolButton(parent);
    btn->setIcon(makeIcon(QStringLiteral(":/icons/%1.png").arg(iconName)));
    btn->setIconSize(QSize(24, 24));
    btn->setFixedSize(28, 28);
    btn->setToolTip(tooltip);
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(QStringLiteral(
        "QToolButton { border: 1px solid transparent; border-radius: 3px; "
        "background: transparent; }"
        "QToolButton:hover { background: %1; }"
        "QToolButton:checked { background: %2; border: 1px solid %3; }")
        .arg(t->colorSurfaceHover.name())
        .arg(t->colorSurfaceSelected.name())
        .arg(t->colorAccent.name()));
    return btn;
}

PropertiesPanel::PropertiesPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("propertiesPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    // Scrollable container: when the dock is shrunk below the active view's
    // minimum height, its controls overflow with a vertical scrollbar instead
    // of being clipped. Frameless; the wrapper itself is painted colorSurface
    // (see applyWrapperSurface) — scoped by objectName so child controls keep
    // their own theme and aren't force-styled.
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("propertiesScroll"));
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    outer->addWidget(m_scroll);

    auto* content = new QWidget(m_scroll);
    content->setObjectName(QStringLiteral("propertiesScrollContent"));
    m_scroll->setWidget(content);

    applyWrapperSurface();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &PropertiesPanel::applyWrapperSurface);

    auto* contentLay = new QVBoxLayout(content);
    contentLay->setContentsMargins(0, 0, 0, 0);

    // The adjustment editors live alongside the default layer view; only one
    // view is visible at a time (showCurvesEditor / showColorBalanceEditor /
    // showHueSaturationEditor / showLayerProperties).
    m_curvesEditor = new CurvesEditorWidget(content);
    m_curvesEditor->setVisible(false);
    contentLay->addWidget(m_curvesEditor);

    m_colorBalanceEditor = new ColorBalanceAdjustmentWidget(content);
    m_colorBalanceEditor->setVisible(false);
    contentLay->addWidget(m_colorBalanceEditor);

    m_hueSaturationEditor = new HueSaturationAdjustmentWidget(content);
    m_hueSaturationEditor->setVisible(false);
    contentLay->addWidget(m_hueSaturationEditor);

    m_solidColorEditor = new SolidColorAdjustmentWidget(content);
    m_solidColorEditor->setVisible(false);
    contentLay->addWidget(m_solidColorEditor);

    // Document/Canvas page — shown when no layer is selected.
    m_documentSection = new QWidget(content);
    m_documentSection->setVisible(false);
    contentLay->addWidget(m_documentSection);
    {
        auto* docLay = new QVBoxLayout(m_documentSection);
        docLay->setContentsMargins(0, 0, 0, 0);
        docLay->setSpacing(0);
        buildDocumentInterface(docLay);
    }

    m_layerSection = new QWidget(content);
    contentLay->addWidget(m_layerSection);

    auto* lay = new QVBoxLayout(m_layerSection);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    buildTransformInterface(lay);

    // ── Layer Mask section ──
    m_maskSection = new CollapsibleSection(tr("Layer Mask"), m_layerSection);
    m_maskSection->setVisible(false);
    auto* maskLay = m_maskSection->body();

    auto* densityRow = new QHBoxLayout();
    auto* densityTitle = new QLabel(tr("Density:"), m_maskSection);
    m_densitySlider = new QSlider(Qt::Horizontal, m_maskSection);
    m_densitySlider->setRange(0, 100);
    m_densitySlider->setValue(100);
    m_densityLabel = new QLabel("100%", m_maskSection);
    densityRow->addWidget(densityTitle);
    densityRow->addWidget(m_densitySlider, 1);
    densityRow->addWidget(m_densityLabel);
    maskLay->addLayout(densityRow);

    auto* featherRow = new QHBoxLayout();
    auto* featherTitle = new QLabel(tr("Feather:"), m_maskSection);
    m_featherSlider = new QSlider(Qt::Horizontal, m_maskSection);
    m_featherSlider->setRange(0, 250);
    m_featherSlider->setValue(0);
    m_featherLabel = new QLabel("0px", m_maskSection);
    featherRow->addWidget(featherTitle);
    featherRow->addWidget(m_featherSlider, 1);
    featherRow->addWidget(m_featherLabel);
    maskLay->addLayout(featherRow);

    m_invertButton = new QPushButton(tr("Invert Mask"), m_maskSection);
    maskLay->addWidget(m_invertButton);

    m_overlayCheck = new AppCheckBox(m_maskSection);
    m_overlayCheck->setText(tr("Show Overlay"));
    maskLay->addWidget(m_overlayCheck);

    m_overlayOpacityTitle = new QLabel(tr("Overlay Opacity"), m_maskSection);
    maskLay->addWidget(m_overlayOpacityTitle);
    m_overlayOpacity = new ScrubbableValueInput(QString(), 0.0, 100.0, 50.0,
                                                QStringLiteral("%"), 1.0, m_maskSection);
    maskLay->addWidget(m_overlayOpacity);

    lay->addWidget(m_maskSection);
    lay->addStretch();

    // Mask connections
    connect(m_invertButton, &QPushButton::clicked, this, [this]() {
        emit maskInvertRequested();
    });
    connect(m_overlayCheck, &QCheckBox::toggled, this, [this](bool checked) {
        emit maskOverlayToggled(checked);
    });
    connect(m_overlayOpacity, &ScrubbableValueInput::valueChanged, this, [this](double val) {
        emit maskOverlayOpacityChanged(static_cast<float>(val) / 100.0f);
    });
    connect(m_densitySlider, &QSlider::valueChanged, this, [this](int val) {
        m_densityLabel->setText(QString("%1%").arg(val));
        emit maskDensityChanged(static_cast<float>(val) / 100.0f);
    });
    connect(m_featherSlider, &QSlider::sliderPressed, this, [this]() {
        emit maskFeatherBegin();
    });
    connect(m_featherSlider, &QSlider::valueChanged, this, [this](int val) {
        m_featherLabel->setText(QString("%1px").arg(val));
        emit maskFeatherPreview(static_cast<float>(val));
    });
    connect(m_featherSlider, &QSlider::sliderReleased, this, [this]() {
        emit maskFeatherCommit(static_cast<float>(m_featherSlider->value()));
    });
}

void PropertiesPanel::buildTransformInterface(QVBoxLayout* parentLayout)
{
    auto* t = ThemeManager::instance()->current();

    // ── Header: layer-type icon + name ──
    m_headerRow = new QWidget(m_layerSection);
    auto* headerLay = new QHBoxLayout(m_headerRow);
    headerLay->setContentsMargins(t->spaceMD, t->spaceSM, t->spaceMD, t->spaceSM);
    headerLay->setSpacing(t->spaceSM);
    m_headerIcon = new QLabel(m_headerRow);
    m_headerIcon->setFixedSize(18, 18);
    m_headerIcon->setScaledContents(true);
    m_headerLabel = new QLabel(tr("Layer"), m_headerRow);
    m_headerLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; font-weight: bold; }")
                                     .arg(t->colorTextBright.name()));
    headerLay->addWidget(m_headerIcon);
    headerLay->addWidget(m_headerLabel, 1);
    parentLayout->addWidget(m_headerRow);

    // ── Transform section ──
    m_transformSection = new CollapsibleSection(tr("Transform"), m_layerSection);
    {
        m_transformFields = new TransformFieldsWidget(
            TransformFieldsMode::PropertiesPanel, m_transformSection);
        m_transformSection->body()->addWidget(m_transformFields, 0, Qt::AlignHCenter);

        // Rotation + flip row.
        auto* rotRow = new QHBoxLayout();
        rotRow->setSpacing(t->spaceSM);
        m_flipHButton = makeIconButton(QStringLiteral("flip-horizontal"),
                                       tr("Flip horizontal"), m_transformSection);
        m_flipVButton = makeIconButton(QStringLiteral("flip-vertical"),
                                       tr("Flip vertical"), m_transformSection);
        rotRow->addWidget(m_flipHButton);
        rotRow->addWidget(m_flipVButton);
        rotRow->addStretch();
        m_transformSection->body()->addLayout(rotRow);
    }
    parentLayout->addWidget(m_transformSection);

    connect(m_transformFields, &TransformFieldsWidget::fieldEdited,
            this, &PropertiesPanel::transformFieldEdited);
    connect(m_flipHButton, &QPushButton::clicked, this, [this]() {
        emit flipHorizontalRequested();
    });
    connect(m_flipVButton, &QPushButton::clicked, this, [this]() {
        emit flipVerticalRequested();
    });

    // ── Align and Distribute section ──
    m_alignSection = new CollapsibleSection(tr("Align and Distribute"), m_layerSection);
    {
        m_alignBar = new AlignBar(tr("Align"), m_alignSection);
        m_alignSection->body()->addWidget(m_alignBar);

        connect(m_alignBar, &AlignBar::alignLeftClicked, this, [this]() {
            emit alignRequested(0);
        });
        connect(m_alignBar, &AlignBar::alignCenterHClicked, this, [this]() {
            emit alignRequested(1);
        });
        connect(m_alignBar, &AlignBar::alignRightClicked, this, [this]() {
            emit alignRequested(2);
        });
        connect(m_alignBar, &AlignBar::alignTopClicked, this, [this]() {
            emit alignRequested(3);
        });
        connect(m_alignBar, &AlignBar::alignMiddleVClicked, this, [this]() {
            emit alignRequested(4);
        });
        connect(m_alignBar, &AlignBar::alignBottomClicked, this, [this]() {
            emit alignRequested(5);
        });
        connect(m_alignBar, &AlignBar::alignCenterClicked, this, [this]() {
            emit alignRequested(1);
            emit alignRequested(4);
        });
        connect(m_alignBar, &AlignBar::alignTargetChanged,
                this, &PropertiesPanel::alignTargetChanged);
        connect(m_alignBar, &AlignBar::resetTransformClicked,
                this, &PropertiesPanel::resetTransformRequested);
    }
    parentLayout->addWidget(m_alignSection);

    // ── Quick Actions section (Pixel only) ──
    m_quickSection = new CollapsibleSection(tr("Quick Actions"), m_layerSection);
    {
        m_removeBgButton = new QPushButton(tr("Remove Background"), m_quickSection);
        m_selectSubjectButton = new QPushButton(tr("Select Subject"), m_quickSection);
        m_aiUpscaleButton = new QPushButton(tr("AI Upscale"), m_quickSection);
        m_quickSection->body()->addWidget(m_removeBgButton);
        m_quickSection->body()->addWidget(m_selectSubjectButton);
        m_quickSection->body()->addWidget(m_aiUpscaleButton);
    }
    parentLayout->addWidget(m_quickSection);

    connect(m_removeBgButton, &QPushButton::clicked, this, [this]() {
        emit removeBackgroundRequested();
    });
    connect(m_selectSubjectButton, &QPushButton::clicked, this, [this]() {
        emit selectSubjectRequested();
    });
    connect(m_aiUpscaleButton, &QPushButton::clicked, this, [this]() {
        emit aiUpscaleRequested();
    });

    // Hidden until a supported layer is selected.
    m_headerRow->setVisible(false);
    m_transformSection->setVisible(false);
    m_alignSection->setVisible(false);
    m_quickSection->setVisible(false);
}

void PropertiesPanel::buildDocumentInterface(QVBoxLayout* parentLayout)
{
    auto* t = ThemeManager::instance()->current();

    auto secondaryLabel = [&](const QString& text, QWidget* parent) {
        auto* l = new QLabel(text, parent);
        l->setStyleSheet(QStringLiteral("QLabel { color: %1; }")
                             .arg(t->colorTextSecondary.name()));
        return l;
    };

    // ── Header: document icon + "Document" ──
    auto* header = new QWidget(m_documentSection);
    auto* headerLay = new QHBoxLayout(header);
    headerLay->setContentsMargins(t->spaceMD, t->spaceSM, t->spaceMD, t->spaceSM);
    headerLay->setSpacing(t->spaceSM);
    auto* docIcon = new QLabel(header);
    docIcon->setFixedSize(18, 18);
    docIcon->setScaledContents(true);
    docIcon->setPixmap(QPixmap(QStringLiteral(":/icons/prop-document.png")));
    auto* docLabel = new QLabel(tr("Document"), header);
    docLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; font-weight: bold; }")
                                .arg(t->colorTextBright.name()));
    headerLay->addWidget(docIcon);
    headerLay->addWidget(docLabel, 1);
    parentLayout->addWidget(header);

    // ── Canvas section ──
    auto* canvasSection = new CollapsibleSection(tr("Canvas"), m_documentSection);
    {
        auto makeField = [&](const QString& label, double mn, double mx, int decimals,
                             const QString& suffix) {
            auto* l = secondaryLabel(label + QStringLiteral(":"), canvasSection);
            auto* input = new QDoubleSpinBox(canvasSection);
            input->setRange(mn, mx);
            input->setDecimals(decimals);
            input->setSingleStep(1.0);
            input->setSuffix(suffix);
            input->setButtonSymbols(QAbstractSpinBox::NoButtons);
            input->setKeyboardTracking(false);
            input->setAlignment(Qt::AlignRight);
            input->setFixedHeight(24);
            return std::pair<QLabel*, QDoubleSpinBox*>(l, input);
        };

        auto [wLabel, wInput] = makeField(tr("W"), 1.0, 100000.0, 0, tr(" px"));
        auto [hLabel, hInput] = makeField(tr("H"), 1.0, 100000.0, 0, tr(" px"));
        auto [xLabel, xInput] = makeField(tr("X"), -100000.0, 100000.0, 0, tr(" px"));
        auto [yLabel, yInput] = makeField(tr("Y"), -100000.0, 100000.0, 0, tr(" px"));
        m_canvasWInput = wInput;
        m_canvasHInput = hInput;
        m_canvasXInput = xInput;
        m_canvasYInput = yInput;
        for (auto* input : { m_canvasWInput, m_canvasHInput, m_canvasXInput, m_canvasYInput })
            input->setFixedWidth(100);

        m_canvasLinkButton = makeIconButton(QStringLiteral("constrain-proportions"),
                                            tr("Link width and height (constrain proportions)"),
                                            canvasSection);
        m_canvasLinkButton->setCheckable(true);

        auto* canvasGridWrap = new QWidget(canvasSection);
        auto* grid = new QGridLayout(canvasGridWrap);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(t->spaceSM);
        grid->setVerticalSpacing(t->spaceXS);
        grid->addWidget(m_canvasLinkButton, 0, 0, 2, 1);
        grid->addWidget(wLabel, 0, 1);
        grid->addWidget(m_canvasWInput, 0, 2, Qt::AlignLeft);
        grid->addWidget(xLabel, 0, 3);
        grid->addWidget(m_canvasXInput, 0, 4, Qt::AlignLeft);
        grid->addWidget(hLabel, 1, 1);
        grid->addWidget(m_canvasHInput, 1, 2, Qt::AlignLeft);
        grid->addWidget(yLabel, 1, 3);
        grid->addWidget(m_canvasYInput, 1, 4, Qt::AlignLeft);
        canvasSection->body()->addWidget(canvasGridWrap, 0, Qt::AlignHCenter);

        // The canvas has no offset concept — X/Y are informative only.
        m_canvasXInput->setEnabled(false);
        m_canvasYInput->setEnabled(false);

        // Resolution / Mode / Bit depth rows.
        auto* formWrap = new QWidget(canvasSection);
        formWrap->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto* form = new QGridLayout(formWrap);
        form->setContentsMargins(0, 0, 0, 0);
        form->setHorizontalSpacing(t->spaceSM);
        form->setVerticalSpacing(t->spaceXS);
        form->setColumnStretch(2, 1);

        m_resolutionInput = new ScrubbableValueInput(QString(), 1.0, 10000.0, 300.0,
                                                     tr(" px/in"), 1.0, canvasSection);
        m_resolutionInput->setDecimals(0);
        m_resolutionUnitCombo = new AppComboBox(canvasSection);
        m_resolutionUnitCombo->addItem(tr("Pixels/Inch"));
        m_resolutionUnitCombo->addItem(tr("Pixels/Centimeter"));
        form->addWidget(secondaryLabel(tr("Resolution"), canvasSection), 0, 0);
        form->addWidget(m_resolutionInput, 0, 1, Qt::AlignLeft);
        form->addWidget(m_resolutionUnitCombo, 0, 2, Qt::AlignLeft);

        m_modeCombo = new AppComboBox(canvasSection);
        m_modeCombo->addItem(tr("RGB Color"), QStringLiteral("RGB Color"));
        m_modeCombo->addItem(tr("CMYK Color"), QStringLiteral("CMYK Color"));
        m_modeCombo->addItem(tr("Grayscale"), QStringLiteral("Grayscale"));
        m_modeCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        form->addWidget(secondaryLabel(tr("Mode"), canvasSection), 1, 0);
        form->addWidget(m_modeCombo, 1, 1, 1, 2);

        m_depthCombo = new AppComboBox(canvasSection);
        m_depthCombo->addItem(tr("8 Bits/Channel"), 8);
        m_depthCombo->addItem(tr("16 Bits/Channel"), 16);
        m_depthCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        form->addWidget(secondaryLabel(tr("Depth"), canvasSection), 2, 0);
        form->addWidget(m_depthCombo, 2, 1, 1, 2);

        canvasSection->body()->addWidget(formWrap);
    }
    parentLayout->addWidget(canvasSection);

    // ── Aspect Ratio section ──
    auto* aspectSection = new CollapsibleSection(tr("Aspect Ratio"), m_documentSection);
    {
        auto* orientRow = new QHBoxLayout();
        orientRow->setSpacing(t->spaceXS);
        orientRow->addStretch();

        m_portraitButton = makeToggleButton(QStringLiteral("canvas-portrait"),
                                            tr("Portrait orientation"), aspectSection);
        m_landscapeButton = makeToggleButton(QStringLiteral("canvas-landscape"),
                                             tr("Landscape orientation"), aspectSection);
        for (auto* button : { m_portraitButton, m_landscapeButton }) {
            button->setIconSize(QSize(34, 34));
            button->setFixedSize(36, 36);
            button->setStyleSheet(button->styleSheet() + QStringLiteral(
                "QToolButton { padding: 0px; }"));
        }
        orientRow->addWidget(m_portraitButton);
        orientRow->addWidget(m_landscapeButton);
        orientRow->addStretch();
        aspectSection->body()->addLayout(orientRow);
    }
    parentLayout->addWidget(aspectSection);

    connect(m_canvasWInput, &QDoubleSpinBox::editingFinished, this, [this]() {
        emit canvasSizeEdited(FieldWidth, m_canvasWInput->value());
    });
    connect(m_canvasHInput, &QDoubleSpinBox::editingFinished, this, [this]() {
        emit canvasSizeEdited(FieldHeight, m_canvasHInput->value());
    });
    connect(m_resolutionInput, &ScrubbableValueInput::editingFinished, this, [this](double v) {
        const double dpi = (m_resolutionUnitCombo && m_resolutionUnitCombo->currentIndex() == 1)
                               ? v * 2.54
                               : v;
        emit canvasResolutionEdited(dpi);
    });
    connect(m_resolutionUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        if (!m_resolutionInput)
            return;
        QSignalBlocker block(m_resolutionInput);
        m_resolutionInput->setSuffix(idx == 1 ? tr(" px/cm") : tr(" px/in"));
        m_resolutionInput->setValue(idx == 1
                                        ? m_documentResolutionDpi / 2.54
                                        : m_documentResolutionDpi);
    });
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        if (idx < 0)
            return;
        emit canvasColorModeChanged(m_modeCombo->itemData(idx).toString());
    });
    connect(m_depthCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        if (idx < 0)
            return;
        emit canvasBitDepthChanged(m_depthCombo->itemData(idx).toInt());
    });
    connect(m_portraitButton, &QToolButton::clicked, this, [this]() {
        emit canvasPortraitRequested();
    });
    connect(m_landscapeButton, &QToolButton::clicked, this, [this]() {
        emit canvasLandscapeRequested();
    });

    // ── Rulers & Grids section ──
    auto* rulersSection = new CollapsibleSection(tr("Rulers && Grids"), m_documentSection);
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(t->spaceXS);
        m_rulersToggle = makeToggleButton(QStringLiteral("ui-rulers"),
                                          tr("Show rulers"), rulersSection);
        row->addWidget(m_rulersToggle);
        row->addStretch();
        rulersSection->body()->addLayout(row);

        auto* unitRow = new QGridLayout();
        unitRow->setHorizontalSpacing(t->spaceSM);
        unitRow->setColumnStretch(1, 1);
        m_unitCombo = new AppComboBox(rulersSection);
        // Order matches the RulerUnit enum (Pixels, Percent, Inches, Centimeters,
        // Millimeters) so the combo index maps directly onto the enum value.
        m_unitCombo->addItem(tr("Pixels"));
        m_unitCombo->addItem(tr("Percent"));
        m_unitCombo->addItem(tr("Inches"));
        m_unitCombo->addItem(tr("Centimeters"));
        m_unitCombo->addItem(tr("Millimeters"));
        unitRow->addWidget(secondaryLabel(tr("Units"), rulersSection), 0, 0);
        unitRow->addWidget(m_unitCombo, 0, 1);
        rulersSection->body()->addLayout(unitRow);
    }
    parentLayout->addWidget(rulersSection);
    parentLayout->addStretch();

    connect(m_rulersToggle, &QToolButton::toggled, this, [this](bool on) {
        emit rulersToggled(on);
    });
    connect(m_unitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int idx) {
        emit rulerUnitChanged(idx);
    });
}

void PropertiesPanel::applyWrapperSurface()
{
    if (!m_scroll)
        return;
    const QString surface = ThemeManager::instance()->current()->colorSurface.name();
    // Object-name selectors match only the scroll wrapper and its content
    // backdrop — never the editor children or their themed controls — so the
    // surface colour is applied to the wrapper alone and nothing is force-styled.
    m_scroll->setStyleSheet(QStringLiteral(
        "QScrollArea#propertiesScroll { background: %1; border: none; }"
        "QWidget#propertiesScrollContent { background: %1; }").arg(surface));
}

void PropertiesPanel::setController(ImageController* ctrl)
{
    m_controller = ctrl;
    if (m_curvesEditor)
        m_curvesEditor->setController(ctrl);
    if (m_colorBalanceEditor)
        m_colorBalanceEditor->setController(ctrl);
    if (m_hueSaturationEditor)
        m_hueSaturationEditor->setController(ctrl);
    if (m_solidColorEditor)
        m_solidColorEditor->setController(ctrl);
}

void PropertiesPanel::showCurvesEditor(int flatIndex)
{
    m_documentSection->setVisible(false);
    m_layerSection->setVisible(false);
    m_colorBalanceEditor->setVisible(false);
    m_hueSaturationEditor->setVisible(false);
    m_solidColorEditor->setVisible(false);
    m_curvesEditor->setVisible(true);
    m_curvesEditor->showNode(flatIndex);
}

void PropertiesPanel::showColorBalanceEditor(int flatIndex)
{
    m_documentSection->setVisible(false);
    m_layerSection->setVisible(false);
    m_curvesEditor->setVisible(false);
    m_hueSaturationEditor->setVisible(false);
    m_solidColorEditor->setVisible(false);
    m_colorBalanceEditor->setVisible(true);
    m_colorBalanceEditor->showNode(flatIndex);
}

void PropertiesPanel::showHueSaturationEditor(int flatIndex)
{
    m_documentSection->setVisible(false);
    m_layerSection->setVisible(false);
    m_curvesEditor->setVisible(false);
    m_colorBalanceEditor->setVisible(false);
    m_solidColorEditor->setVisible(false);
    m_hueSaturationEditor->setVisible(true);
    m_hueSaturationEditor->showNode(flatIndex);
}

void PropertiesPanel::showSolidColorEditor(int flatIndex)
{
    m_documentSection->setVisible(false);
    m_layerSection->setVisible(false);
    m_curvesEditor->setVisible(false);
    m_colorBalanceEditor->setVisible(false);
    m_hueSaturationEditor->setVisible(false);
    m_solidColorEditor->setVisible(true);
    m_solidColorEditor->showNode(flatIndex);
}

void PropertiesPanel::openSolidColorPicker()
{
    // The editor is bound via showSolidColorEditor() before this call; openColorPicker
    // is a no-op when it isn't bound to a node, so no visibility guard is needed.
    if (m_solidColorEditor)
        m_solidColorEditor->openColorPicker();
}

void PropertiesPanel::showLayerProperties()
{
    if (m_curvesEditor->isVisible())
        m_curvesEditor->setVisible(false);
    if (m_colorBalanceEditor->isVisible())
        m_colorBalanceEditor->setVisible(false);
    if (m_hueSaturationEditor->isVisible())
        m_hueSaturationEditor->setVisible(false);
    if (m_solidColorEditor->isVisible())
        m_solidColorEditor->setVisible(false);
    if (m_documentSection->isVisible())
        m_documentSection->setVisible(false);
    m_layerSection->setVisible(true);
}

void PropertiesPanel::showDocumentProperties()
{
    if (m_curvesEditor->isVisible())
        m_curvesEditor->setVisible(false);
    if (m_colorBalanceEditor->isVisible())
        m_colorBalanceEditor->setVisible(false);
    if (m_hueSaturationEditor->isVisible())
        m_hueSaturationEditor->setVisible(false);
    if (m_solidColorEditor->isVisible())
        m_solidColorEditor->setVisible(false);
    if (m_layerSection->isVisible())
        m_layerSection->setVisible(false);
    m_documentSection->setVisible(true);
}

bool PropertiesPanel::proportionsLocked() const
{
    return m_transformFields && m_transformFields->proportionsLocked();
}

bool PropertiesPanel::canvasProportionsLocked() const
{
    return m_canvasLinkButton && m_canvasLinkButton->isChecked();
}

void PropertiesPanel::setDocumentInfo(int widthPx, int heightPx, double resolutionDpi,
                                      const QString& colorMode, int bitDepth,
                                      bool portraitActive, bool landscapeActive)
{
    // setValue does not emit, but the orientation toggles drive on `clicked`
    // (which fires on user action only) so blocking is belt-and-suspenders.
    m_canvasWInput->setValue(widthPx);
    m_canvasHInput->setValue(heightPx);
    m_canvasXInput->setValue(0.0);
    m_canvasYInput->setValue(0.0);
    m_documentResolutionDpi = resolutionDpi;
    {
        QSignalBlocker block(m_resolutionInput);
        const bool pxPerCm = m_resolutionUnitCombo && m_resolutionUnitCombo->currentIndex() == 1;
        m_resolutionInput->setSuffix(pxPerCm ? tr(" px/cm") : tr(" px/in"));
        m_resolutionInput->setValue(pxPerCm ? resolutionDpi / 2.54 : resolutionDpi);
    }
    if (m_modeCombo) {
        QSignalBlocker block(m_modeCombo);
        const int idx = m_modeCombo->findData(colorMode);
        if (idx >= 0)
            m_modeCombo->setCurrentIndex(idx);
    }
    if (m_depthCombo) {
        QSignalBlocker block(m_depthCombo);
        const int idx = m_depthCombo->findData(bitDepth);
        if (idx >= 0)
            m_depthCombo->setCurrentIndex(idx);
    }

    {
        QSignalBlocker pb(m_portraitButton);
        QSignalBlocker lb(m_landscapeButton);
        m_portraitButton->setChecked(portraitActive);
        m_landscapeButton->setChecked(landscapeActive);
    }
}

void PropertiesPanel::setRulerGuideState(bool rulersVisible, int unit)
{
    if (m_rulersToggle) {
        QSignalBlocker b(m_rulersToggle);
        m_rulersToggle->setChecked(rulersVisible);
    }
    if (m_unitCombo && unit >= 0 && unit < m_unitCombo->count()) {
        QSignalBlocker b(m_unitCombo);
        m_unitCombo->setCurrentIndex(unit);
    }
}

void PropertiesPanel::setLayerTransformInfo(LayerKind kind,
                                            double widthPx, double heightPx,
                                            double posX, double posY,
                                            double rotationDeg,
                                            bool canTransform)
{
    const bool supported = (kind != LayerKind::None);
    m_headerRow->setVisible(supported);
    m_transformSection->setVisible(supported);
    m_alignSection->setVisible(supported);
    // Quick Actions only make sense for a Pixel layer (AI subject/background).
    m_quickSection->setVisible(kind == LayerKind::Pixel);

    if (!supported)
        return;

    // Header icon + label per type.
    QString iconName, typeName;
    switch (kind) {
    case LayerKind::Text:  iconName = QStringLiteral("text");  typeName = tr("Text Layer");  break;
    case LayerKind::Shape: iconName = QStringLiteral("shape"); typeName = tr("Shape Layer"); break;
    case LayerKind::Pixel:
    default:               iconName = QStringLiteral("prop-pixel-layer"); typeName = tr("Pixel Layer"); break;
    }
    m_headerIcon->setPixmap(QPixmap(QStringLiteral(":/icons/%1.png").arg(iconName)));
    m_headerLabel->setText(typeName);

    // Sync values without re-triggering edits (setValue does not emit signals).
    m_transformFields->setTransformValues(widthPx, heightPx, posX, posY, rotationDeg);

    // A hard position lock disables editing/aligning but still shows the values.
    m_transformFields->setFieldsEnabled(canTransform);
    m_flipHButton->setEnabled(canTransform);
    m_flipVButton->setEnabled(canTransform);
    if (m_alignBar)
        m_alignBar->updateButtons(canTransform);
}

void PropertiesPanel::setMaskInfo(bool hasMask, float density, float feather)
{
    if (m_maskSection)
        m_maskSection->setVisible(hasMask);

    if (hasMask) {
        m_densitySlider->blockSignals(true);
        m_densitySlider->setValue(static_cast<int>(density * 100.0f));
        m_densitySlider->blockSignals(false);
        m_densityLabel->setText(QString("%1%").arg(static_cast<int>(density * 100.0f)));

        m_featherSlider->blockSignals(true);
        m_featherSlider->setValue(static_cast<int>(feather));
        m_featherSlider->blockSignals(false);
        m_featherLabel->setText(QString("%1px").arg(static_cast<int>(feather)));
    }
}

void PropertiesPanel::setMaskOverlayState(bool visible, float opacity)
{
    m_overlayCheck->blockSignals(true);
    m_overlayCheck->setChecked(visible);
    m_overlayCheck->blockSignals(false);

    m_overlayOpacity->blockSignals(true);
    m_overlayOpacity->setValue(static_cast<double>(opacity) * 100.0);
    m_overlayOpacity->blockSignals(false);
    // Opacity stays editable even when the overlay is off (per spec).
}

void PropertiesPanel::clear()
{
    // Empty state (no document at all): hide the Document page and fall back to
    // an empty layer view.
    showLayerProperties();
    setLayerTransformInfo(LayerKind::None, 0, 0, 0, 0, 0, false);
    setMaskInfo(false, 1.0f, 0.0f);
}
