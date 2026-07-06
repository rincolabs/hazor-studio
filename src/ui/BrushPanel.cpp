#include "BrushPanel.hpp"
#include "BrushLibraryListView.hpp"
#include "BrushTipPreview.hpp"
#include "brush/BrushPresetManager.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QSlider>
#include <QFrame>
#include <QToolButton>
#include <QMenu>
#include <QSignalBlocker>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>

namespace {
// Extensions the brush import pipeline accepts (mirrors the adapters). Used to
// decide whether a drag of files onto the panel should be accepted.
bool isBrushDropFile(const QString& path)
{
    static const QStringList exts = {
        QStringLiteral("kpp"), QStringLiteral("paintoppreset"),
        QStringLiteral("bundle"), QStringLiteral("png"), QStringLiteral("jpg"),
        QStringLiteral("jpeg"), QStringLiteral("bmp"), QStringLiteral("webp")
    };
    return exts.contains(QFileInfo(path).suffix().toLower());
}
} // namespace

BrushPanel::BrushPanel(QWidget* parent)
    : QWidget(parent)
{
    const Theme* t = ThemeManager::instance()->current();
    setAttribute(Qt::WA_StyledBackground, true);
    setAcceptDrops(true); // brush files can be dropped here to import

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(t->spaceSM, t->spaceSM, t->spaceSM, t->spaceSM);
    root->setSpacing(t->spaceSM);

    // ── Header: panel menu (≡ button in the corner) ──
    auto* header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    auto* title = new QLabel(tr("Brushes"), this);
    title->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;").arg(t->colorTextPrimary.name()));
    header->addWidget(title);
    header->addStretch(1);
    auto* menuBtn = new QToolButton(this);
    menuBtn->setText(QStringLiteral("☰")); // ≡
    menuBtn->setToolTip(tr("Brush panel menu"));
    menuBtn->setPopupMode(QToolButton::InstantPopup);
    menuBtn->setAutoRaise(true);
    auto* menu = new QMenu(menuBtn);
    QAction* importAct = menu->addAction(tr("Import Brushes…"));
    connect(importAct, &QAction::triggered, this, &BrushPanel::importBrushesRequested);
    menuBtn->setMenu(menu);
    header->addWidget(menuBtn);
    root->addLayout(header);

    // ── Area 1: Brush quick settings ──
    auto* quick = new QWidget(this);
    auto* quickLay = new QHBoxLayout(quick);
    quickLay->setContentsMargins(0, 0, 0, 0);
    quickLay->setSpacing(t->spaceMD);

    m_tipPreview = new BrushTipPreview(quick);
    quickLay->addWidget(m_tipPreview);

    auto* readout = new QWidget(quick);
    auto* grid = new QGridLayout(readout);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(t->spaceSM);
    grid->setVerticalSpacing(t->spaceXS);
    grid->setColumnStretch(1, 1);

    auto* sizeCaption = new QLabel(tr("Size"), readout);
    auto* hardnessCaption = new QLabel(tr("Hardness"), readout);
    sizeCaption->setStyleSheet(QStringLiteral("color: %1;").arg(t->colorTextSecondary.name()));
    hardnessCaption->setStyleSheet(QStringLiteral("color: %1;").arg(t->colorTextSecondary.name()));

    m_sizeSlider = new QSlider(Qt::Horizontal, readout);
    m_sizeSlider->setRange(1, kSizeSliderMax);
    m_hardnessSlider = new QSlider(Qt::Horizontal, readout);
    m_hardnessSlider->setRange(0, 100);

    m_sizeValue = new QLabel(readout);
    m_hardnessValue = new QLabel(readout);
    m_sizeValue->setMinimumWidth(48);
    m_hardnessValue->setMinimumWidth(48);
    m_sizeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_hardnessValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_sizeValue->setStyleSheet(QStringLiteral("color: %1;").arg(t->colorTextPrimary.name()));
    m_hardnessValue->setStyleSheet(QStringLiteral("color: %1;").arg(t->colorTextPrimary.name()));

    grid->addWidget(sizeCaption, 0, 0);
    grid->addWidget(m_sizeSlider, 0, 1);
    grid->addWidget(m_sizeValue, 0, 2);
    grid->addWidget(hardnessCaption, 1, 0);
    grid->addWidget(m_hardnessSlider, 1, 1);
    grid->addWidget(m_hardnessValue, 1, 2);
    quickLay->addWidget(readout, 1);

    root->addWidget(quick);

    connect(m_sizeSlider, &QSlider::valueChanged, this, [this](int v) {
        applySize(static_cast<float>(v), true);
    });
    connect(m_hardnessSlider, &QSlider::valueChanged, this, [this](int v) {
        applyHardness(v / 100.0f, true);
    });

    auto* sep1 = new QFrame(this);
    sep1->setFrameShape(QFrame::HLine);
    sep1->setStyleSheet(QStringLiteral("color: %1;").arg(t->colorBorder.name()));
    root->addWidget(sep1);

    // ── Area 2: shared preset library (search + folder tree) ──
    // Selector mode: list + select only, no preset/folder administration.
    m_list = new BrushLibraryListView(BrushPresetListMode::BrushPanelSelector, this);
    root->addWidget(m_list, 1);

    connect(m_list, &BrushLibraryListView::presetSelected, this,
            [this](const BrushPreset& p) {
                setBrush(p.settings);
                emit presetSelected(p);
            });
    connect(m_list, &BrushLibraryListView::importBrushesRequested,
            this, &BrushPanel::importBrushesRequested);
    connect(m_list, &BrushLibraryListView::openInSettingsRequested,
            this, &BrushPanel::openInSettingsRequested);

    connect(m_tipPreview, &BrushTipPreview::sizeChanged, this, [this](float s) {
        applySize(s, true);
    });
    connect(m_tipPreview, &BrushTipPreview::hardnessChanged, this, [this](float h) {
        applyHardness(h, true);
    });

    applySize(m_tipPreview->brushSize(), false);
    applyHardness(m_tipPreview->hardness(), false);

    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &BrushPanel::applyTheme);
    applyTheme();
}

BrushPanel::~BrushPanel() = default;

void BrushPanel::setPresetManager(BrushPresetManager* manager)
{
    m_list->setPresetManager(manager);
    if (manager && !manager->presets().empty())
        setBrush(manager->presets().front().settings);
}

void BrushPanel::applyTheme()
{
    const Theme* t = ThemeManager::instance()->current();
    setStyleSheet(QStringLiteral("BrushPanel { background: %1; color: %2; }")
        .arg(t->colorSurface.name(), t->colorTextPrimary.name()));
    if (m_sizeValue)
        m_sizeValue->setStyleSheet(QStringLiteral("color: %1;").arg(t->colorTextPrimary.name()));
    if (m_hardnessValue)
        m_hardnessValue->setStyleSheet(QStringLiteral("color: %1;").arg(t->colorTextPrimary.name()));
    if (m_tipPreview)
        m_tipPreview->update();
}

void BrushPanel::warmCacheAsync()
{
    if (m_list)
        m_list->warmCacheAsync();
}

void BrushPanel::setCurrentPreset(const QString& name)
{
    if (m_list)
        m_list->setCurrentPreset(name);
}

void BrushPanel::revealPreset(const QString& name)
{
    if (m_list)
        m_list->revealPreset(name);
}

void BrushPanel::setBrush(const BrushSettings& settings)
{
    // setBrush also carries tip type/source, which the sliders don't cover.
    m_tipPreview->setBrush(settings);
    applySize(settings.size, false);
    applyHardness(settings.hardness, false);
}

void BrushPanel::setSize(float size)
{
    applySize(size, false);
}

void BrushPanel::setHardness(float hardness)
{
    applyHardness(hardness, false);
}

void BrushPanel::applySize(float size, bool emitSignal)
{
    const int rounded = qBound(1, qRound(size), kSizeSliderMax);
    m_tipPreview->setSize(static_cast<float>(rounded));
    if (m_sizeSlider->value() != rounded) {
        QSignalBlocker block(m_sizeSlider);
        m_sizeSlider->setValue(rounded);
    }
    m_sizeValue->setText(tr("%1 px").arg(rounded));
    if (emitSignal)
        emit brushSizeChanged(static_cast<float>(rounded));
}

void BrushPanel::applyHardness(float hardness, bool emitSignal)
{
    const int pct = qBound(0, qRound(hardness * 100.0f), 100);
    m_tipPreview->setHardness(pct / 100.0f);
    if (m_hardnessSlider->value() != pct) {
        QSignalBlocker block(m_hardnessSlider);
        m_hardnessSlider->setValue(pct);
    }
    m_hardnessValue->setText(tr("%1%").arg(pct));
    if (emitSignal)
        emit brushHardnessChanged(pct / 100.0f);
}

void BrushPanel::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls()) return;
    for (const QUrl& u : event->mimeData()->urls()) {
        if (u.isLocalFile() && isBrushDropFile(u.toLocalFile())) {
            event->acceptProposedAction();
            return;
        }
    }
}

void BrushPanel::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasUrls()) return;
    QStringList paths;
    for (const QUrl& u : event->mimeData()->urls()) {
        if (!u.isLocalFile()) continue;
        const QString p = u.toLocalFile();
        if (isBrushDropFile(p)) paths << p;
    }
    if (!paths.isEmpty()) {
        event->acceptProposedAction();
        emit brushFilesDropped(paths);
    }
}
