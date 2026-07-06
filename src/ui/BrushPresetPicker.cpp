#include "BrushPresetPicker.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "theme/ScrollBarStyle.hpp"

#include <QMenu>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QPainter>
#include <QWidgetAction>
#include <QInputDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QScrollArea>
#include <QFrame>
#include <QStyle>
#include <QByteArray>
#include <cmath>

namespace
{
// Resolve an image-tip preset to a usable QImage, mirroring
// BrushPreviewRenderer::resolveTipImage: prefer the in-memory image, then the
// embedded base64 data, then the file path. Only touches the path when it is
// non-empty, so an embedded-only tip never builds QImage("") (which would emit
// "QFSFileEngine::open: No file name specified").
QImage resolveTipImage(const BrushSettings &s)
{
    if (!s.tipImage.isNull())
        return s.tipImage;
    if (!s.tipImageData.isEmpty())
    {
        QImage img;
        if (img.loadFromData(QByteArray::fromBase64(s.tipImageData.toUtf8())))
            return img;
    }
    if (!s.tipImagePath.isEmpty())
    {
        QImage img(s.tipImagePath);
        if (!img.isNull())
            return img;
    }
    return QImage();
}
} // namespace

BrushPresetPicker::BrushPresetPicker(QWidget *parent)
    : QToolButton(parent)
{
    setPopupMode(QToolButton::MenuButtonPopup);
    setToolTip(tr("Brush Preset Picker"));
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    // setMinimumHeight(38);

    connect(this, &QToolButton::clicked, this, [this]()
            {
        if (m_opensExternalPanel) {
            emit panelRequested();
            return;
        }
        showMenu(); });
}

void BrushPresetPicker::setOpensExternalPanel(bool on)
{
    m_opensExternalPanel = on;
    if (on)
    {
        // Main click requests the dedicated Brush Panel; the arrow keeps a
        // compact actions menu for import.
        setPopupMode(QToolButton::MenuButtonPopup);
        rebuildExternalMenu();
    }
    else
    {
        setPopupMode(QToolButton::MenuButtonPopup);
        rebuildGrid();
    }
}

void BrushPresetPicker::rebuildExternalMenu()
{
    m_popup = new QMenu(this);
    QAction* importAct = m_popup->addAction(tr("Import Brushes..."));
    connect(importAct, &QAction::triggered, this, &BrushPresetPicker::importBrushesRequested);
    setMenu(m_popup);
}

void BrushPresetPicker::setPresetManager(BrushPresetManager *manager)
{
    m_manager = manager;
    if (m_manager)
    {
        connect(m_manager, &BrushPresetManager::presetsChanged, this, &BrushPresetPicker::rebuildGrid);
        rebuildGrid();
    }
}

void BrushPresetPicker::setCurrentPreset(const BrushPreset &preset)
{
    m_currentPresetName = preset.name;
    setToolTip(QStringLiteral("%1 (%2 px)")
                   .arg(preset.name)
                   .arg(static_cast<int>(preset.settings.size)));

    int iw = qMax(width() - 22, qMax(iconSize().width(), 24));
    int ih = qMax(height() - 4, qMax(iconSize().height(), 24));
    setIconSize(QSize(iw, ih));
    setIcon(QIcon(renderThumbnail(preset)));
}

void BrushPresetPicker::rebuildGrid()
{
    if (!m_manager)
        return;
    if (m_opensExternalPanel) {
        rebuildExternalMenu();
        return;
    }
    m_popup = new QMenu(this);
    setMenu(m_popup);

    // Container with fixed size + scroll
    auto *container = new QWidget();
    container->setFixedSize(400, 500);
    {
        auto *t = ThemeManager::instance()->current();
        container->setStyleSheet(QStringLiteral(
                                     "QWidget { background: %1; }")
                                     .arg(t->colorSurface.name()));
    }
    auto *containerLay = new QVBoxLayout(container);
    containerLay->setContentsMargins(0, 0, 0, 0);

    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);
    {
        auto *t = ThemeManager::instance()->current();
        ScrollBarQssOptions sb;
        sb.minLength = 30;
        sb.borderRadius = 4;
        sb.handleMargin = 2;
        sb.horizontal = false;
        scroll->setStyleSheet(
            QStringLiteral("QScrollArea { background: %1; }")
                .arg(t->colorBackgroundSecondary.name())
            + scrollBarQss(t->colorBackgroundTertiary, t->colorSurface, sb));
    }

    auto *grid = new QWidget();
    grid->setAutoFillBackground(true);
    auto *layout = new QGridLayout(grid);
    layout->setSpacing(3);
    layout->setContentsMargins(6, 6, 6, 6);

    const int COLS = 4;
    int col = 0, row = 0;

    auto presets = m_manager->presets();
    for (const auto &p : presets)
    {
        layout->addWidget(buildThumbnailWidget(p), row, col);
        if (++col >= COLS)
        {
            col = 0;
            ++row;
        }
    }

    // Save (+) button
    auto *saveBtn = new QPushButton(grid);
    saveBtn->setFixedSize(56, 56);
    saveBtn->setText(QStringLiteral("+"));
    saveBtn->setToolTip(tr("Save current brush as preset"));
    {
        auto *t = ThemeManager::instance()->current();
        saveBtn->setStyleSheet(QStringLiteral(
                                   "QPushButton { font-size: 24px; font-weight: bold; border: 1px dashed %1; }"
                                   "QPushButton:hover { background: %2; }")
                                   .arg(t->colorBorder.name())
                                   .arg(t->colorSurfaceHover.name()));
    }
    layout->addWidget(saveBtn, row, col);
    ++col;
    if (col >= COLS)
    {
        col = 0;
        ++row;
    }
    connect(saveBtn, &QPushButton::clicked, this, &BrushPresetPicker::savePresetRequested);

    // Load PNG button
    auto *loadBtn = new QPushButton(grid);
    loadBtn->setFixedSize(56, 56);
    loadBtn->setText(QStringLiteral("PNG"));
    loadBtn->setToolTip(tr("Load PNG as brush tip"));
    {
        auto *t = ThemeManager::instance()->current();
        loadBtn->setStyleSheet(QStringLiteral(
                                   "QPushButton { font-size: 9px; font-weight: bold; border: 1px dashed %1; }"
                                   "QPushButton:hover { background: %2; }")
                                   .arg(t->colorBorder.name())
                                   .arg(t->colorSurfaceHover.name()));
    }
    layout->addWidget(loadBtn, row, col);
    connect(loadBtn, &QPushButton::clicked, this, [this]()
            {
        QString path = QFileDialog::getOpenFileName(this, tr("Load Brush Tip"),
            QString(), tr("Images (*.png *.jpg *.bmp *.tiff *.emm)"));
        if (path.isEmpty()) return;
        QImage img(path);
        if (img.isNull()) return;

        QFileInfo fi(path);
        BrushPreset p;
        p.name = fi.baseName();
        p.settings.size = static_cast<float>(qMax(img.width(), img.height()));
        p.settings.type = BrushType::Round;
        p.settings.tipSource = BrushTipSource::Image;
        p.settings.tipImagePath = path;
        p.settings.color = Qt::black;
        if (m_manager) {
            m_manager->addPreset(p);
            if (m_popup) m_popup->close();
            setCurrentPreset(p);
            emit presetSelected(p);
        } });

    scroll->setWidget(grid);
    containerLay->addWidget(scroll);

    auto *action = new QWidgetAction(m_popup);
    action->setDefaultWidget(container);
    m_popup->addAction(action);
    m_popup->addSeparator();
    QAction* importAct = m_popup->addAction(tr("Import Brushes..."));
    connect(importAct, &QAction::triggered, this, &BrushPresetPicker::importBrushesRequested);
}

QWidget *BrushPresetPicker::buildThumbnailWidget(const BrushPreset &preset)
{
    auto *btn = new QPushButton();
    btn->setFixedSize(56, 64);
    btn->setToolTip(QStringLiteral("%1 (%2 px)")
                        .arg(preset.name)
                        .arg(static_cast<int>(preset.settings.size)));
    btn->setFlat(true);
    btn->setCursor(Qt::PointingHandCursor);
    {
        auto *t = ThemeManager::instance()->current();
        btn->setStyleSheet(QStringLiteral(
                               "QPushButton { border: none; background: transparent; padding: 0; }"
                               "QPushButton:hover { background: %1; }")
                               .arg(t->colorSurfaceHover.name()));
    }

    btn->setIcon(QIcon(renderThumbnail(preset)));
    btn->setIconSize(QSize(48, 48));

    auto *lay = new QVBoxLayout(btn);
    lay->setSpacing(0);
    lay->setContentsMargins(1, 1, 1, 1);
    auto *label = new QLabel(preset.name);
    label->setAlignment(Qt::AlignCenter);
    label->setMaximumHeight(14);
    {
        auto *t = ThemeManager::instance()->current();
        label->setStyleSheet(QStringLiteral("font-size: 9px; color: %1;").arg(t->colorTextSecondary.name()));
    }
    lay->addStretch();
    lay->addWidget(label);

    connect(btn, &QPushButton::clicked, this, [this, preset]()
            {
        if (m_popup) m_popup->close();
        setCurrentPreset(preset);
        emit presetSelected(preset); });

    return btn;
}

QPixmap BrushPresetPicker::renderThumbnail(const BrushPreset &preset)
{
    const int S = 64;
    QPixmap pix(S, S);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    // Prefer the preset's own preview (e.g. an imported brush's icon); fall back to
    // rendering the tip below when there is none.
    if (!preset.previewImageData.isEmpty())
    {
        QImage prev;
        if (prev.loadFromData(QByteArray::fromBase64(preset.previewImageData.toLatin1()))
            && !prev.isNull())
        {
            prev = prev.scaled(S, S, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            p.drawImage((S - prev.width()) / 2, (S - prev.height()) / 2, prev);
            return pix;
        }
    }

    if (preset.settings.tipSource == BrushTipSource::Image)
    {
        // Resolve the tip from any of the three sources the engine supports:
        // an in-memory image, embedded base64 data, or a file path. Imported
        // brushes (Krita/GIMP) store the tip as embedded data with an empty
        // tipImagePath, so reading the path alone yielded a blank thumbnail and
        // triggered Qt's "QFSFileEngine::open: No file name specified" warning.
        QImage tip = resolveTipImage(preset.settings);
        if (!tip.isNull())
        {
            tip = tip.scaled(S - 4, S - 4, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            int ox = (S - tip.width()) / 2;
            int oy = (S - tip.height()) / 2;
            p.drawImage(ox, oy, tip);
        }
    }
    else
    {
        float maxSize = static_cast<float>(S - 4);
        float stampSize = std::min(maxSize, preset.settings.size * 2.0f);
        if (stampSize < 4.0f)
            stampSize = 4.0f;
        QRectF r((S - stampSize) / 2.0f, (S - stampSize) / 2.0f,
                 stampSize, stampSize);

        float hardness = preset.settings.hardness;
        if (hardness < 1.0f && preset.settings.type == BrushType::Round)
        {
            QRadialGradient rg(r.center(), stampSize / 2.0f);
            QColor c = preset.settings.color;
            rg.setColorAt(0.0, c);
            rg.setColorAt(hardness, c);
            rg.setColorAt(1.0, QColor(c.red(), c.green(), c.blue(), 0));
            p.setBrush(rg);
            p.setPen(Qt::NoPen);
            p.drawEllipse(r);
        }
        else if (preset.settings.type == BrushType::Round)
        {
            p.setBrush(preset.settings.color);
            p.setPen(Qt::NoPen);
            p.drawEllipse(r);
        }
        else
        {
            p.setBrush(preset.settings.color);
            p.setPen(QPen(Qt::gray, 0.5));
            p.drawRect(r);
        }
    }

    return pix;
}
