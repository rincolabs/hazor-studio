#include "BrushImportDialog.hpp"

#include "brush/import/BrushImportManager.hpp"
#include "brush/import/BrushPresetConverter.hpp"
#include "brush/BrushPresetManager.hpp"
#include "brush/BrushPreviewRenderer.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "ui/AppCheckBox.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QListWidget>
#include <QListView>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QPoint>
#include <QSet>
#include <QIcon>
#include <QPixmap>
#include <QListWidgetItem>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <vector>

namespace {

constexpr int kThumbW = 84;
constexpr int kThumbH = 52;

QString sevColor(const Theme* t, BrushImportSeverity s)
{
    switch (s) {
    case BrushImportSeverity::Error:   return t->colorDanger.name();
    case BrushImportSeverity::Warning: return t->colorWarning.name();
    case BrushImportSeverity::Info:
    default:                           return t->colorTextSecondary.name();
    }
}

} // namespace

bool BrushImportDialog::presetFailed(const DialogPreset& p)
{
    return p.imported.hasError();
}
bool BrushImportDialog::presetPartial(const DialogPreset& p)
{
    const QString& l = p.imported.compatibilityLabel;
    return p.imported.isPartial() || l == QLatin1String("Partial") || l == QLatin1String("Unsupported");
}

BrushImportDialog::BrushImportDialog(BrushImportManager* importManager,
                                     BrushPresetManager* presetManager,
                                     QWidget* parent)
    : QDialog(parent)
    , m_importManager(importManager)
    , m_presetManager(presetManager)
{
    setWindowTitle(tr("Import Brushes"));
    setMinimumSize(860, 600);
    buildUi();
    updateStatus();
}

BrushImportDialog::~BrushImportDialog() = default;

void BrushImportDialog::buildUi()
{
    const Theme* t = ThemeManager::instance()->current();
    setStyleSheet(QStringLiteral("QDialog { background: %1; color: %2; }")
                      .arg(t->colorSurface.name(), t->colorTextPrimary.name()));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(t->spaceMD, t->spaceMD, t->spaceMD, t->spaceMD);
    root->setSpacing(t->spaceSM);

    // ── Top toolbar ──
    auto* tools = new QHBoxLayout();
    m_addBtn = new QPushButton(tr("Add Files…"), this);
    m_removeBtn = new QPushButton(tr("Remove"), this);
    m_clearBtn = new QPushButton(tr("Clear"), this);
    tools->addWidget(m_addBtn);
    tools->addWidget(m_removeBtn);
    tools->addWidget(m_clearBtn);
    tools->addStretch(1);
    root->addLayout(tools);

    // ── Files | Presets splitter ──
    auto* split = new QSplitter(Qt::Horizontal, this);

    auto* filesPane = new QWidget(split);
    auto* filesLay = new QVBoxLayout(filesPane);
    filesLay->setContentsMargins(0, 0, 0, 0);
    filesLay->addWidget(new QLabel(tr("Files"), filesPane));
    m_fileList = new QListWidget(filesPane);
    m_fileList->setUniformItemSizes(false);
    filesLay->addWidget(m_fileList, 1);
    split->addWidget(filesPane);

    auto* presetPane = new QWidget(split);
    auto* presetLay = new QVBoxLayout(presetPane);
    presetLay->setContentsMargins(0, 0, 0, 0);
    presetLay->addWidget(new QLabel(tr("Brush Presets"), presetPane));

    auto* searchRow = new QHBoxLayout();
    m_search = new QLineEdit(presetPane);
    m_search->setPlaceholderText(tr("Search brushes"));
    m_search->setClearButtonEnabled(true);
    m_filter = new QComboBox(presetPane);
    m_filter->addItems({ tr("All"), tr("Compatible"), tr("Partial"), tr("Unsupported"),
                         tr("Selected"), tr("Krita") });
    searchRow->addWidget(m_search, 1);
    searchRow->addWidget(m_filter);
    presetLay->addLayout(searchRow);

    m_presetGrid = new QListWidget(presetPane);
    m_presetGrid->setViewMode(QListView::IconMode);
    m_presetGrid->setResizeMode(QListView::Adjust);
    m_presetGrid->setMovement(QListView::Static);
    m_presetGrid->setSpacing(6);
    m_presetGrid->setIconSize(QSize(kThumbW, kThumbH));
    m_presetGrid->setWordWrap(true);
    presetLay->addWidget(m_presetGrid, 1);

    auto* selRow = new QHBoxLayout();
    m_selAll = new QPushButton(tr("Select All"), presetPane);
    m_selNone = new QPushButton(tr("Select None"), presetPane);
    m_selCompatible = new QPushButton(tr("Select Compatible"), presetPane);
    selRow->addWidget(m_selAll);
    selRow->addWidget(m_selNone);
    selRow->addWidget(m_selCompatible);
    selRow->addStretch(1);
    presetLay->addLayout(selRow);
    split->addWidget(presetPane);

    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 2);
    root->addWidget(split, 1);

    // ── Compatibility detail ──
    m_compat = new QTextBrowser(this);
    m_compat->setMaximumHeight(120);
    m_compat->setOpenExternalLinks(false);
    root->addWidget(m_compat);

    // ── Options row ──
    auto* optBox = new QGroupBox(tr("Compatibility / Options"), this);
    auto* optGrid = new QGridLayout(optBox);

    optGrid->addWidget(new QLabel(tr("Destination:"), optBox), 0, 0);
    m_destination = new QComboBox(optBox);
    m_destination->addItem(tr("Brush Library"));
    m_destination->addItem(tr("Current Session"));
    m_destination->addItem(tr("Current Document"));
    // Per-document resources are not available yet.
    m_destination->setItemData(2, false, Qt::UserRole - 1);
    optGrid->addWidget(m_destination, 0, 1);

    optGrid->addWidget(new QLabel(tr("Duplicates:"), optBox), 0, 2);
    m_duplicates = new QComboBox(optBox);
    m_duplicates->addItems({ tr("Rename imported brushes"), tr("Replace existing brushes"),
                             tr("Skip duplicates"), tr("Ask for each duplicate") });
    optGrid->addWidget(m_duplicates, 0, 3);

    m_groupFromFile = new AppCheckBox(tr("Create group from file name"), optBox);
    m_groupFromFile->setChecked(true);
    optGrid->addWidget(m_groupFromFile, 1, 0, 1, 2);
    m_groupName = new QLineEdit(optBox);
    m_groupName->setPlaceholderText(tr("Group name (optional)"));
    optGrid->addWidget(m_groupName, 1, 2, 1, 2);

    m_preserveGroups = new AppCheckBox(tr("Preserve external groups when available"), optBox);
    m_preserveGroups->setChecked(true);
    optGrid->addWidget(m_preserveGroups, 2, 0, 1, 2);

    m_optSettings = new AppCheckBox(tr("Import compatible settings"), optBox);
    m_optTips = new AppCheckBox(tr("Import brush tips"), optBox);
    m_optTextures = new AppCheckBox(tr("Import textures when possible"), optBox);
    m_optPreviews = new AppCheckBox(tr("Generate stroke previews"), optBox);
    m_optWarnings = new AppCheckBox(tr("Show compatibility warnings after import"), optBox);
    m_optInvertAlpha = new AppCheckBox(tr("Invert imported image alpha"), optBox);
    m_optBasicUnsupported = new AppCheckBox(tr("Create basic preset for unsupported brushes"), optBox);
    for (QCheckBox* c : { m_optSettings, m_optTips, m_optTextures, m_optPreviews,
                          m_optWarnings, m_optBasicUnsupported })
        c->setChecked(true);
    optGrid->addWidget(m_optSettings, 3, 0);
    optGrid->addWidget(m_optTips, 3, 1);
    optGrid->addWidget(m_optTextures, 3, 2);
    optGrid->addWidget(m_optPreviews, 3, 3);
    optGrid->addWidget(m_optWarnings, 4, 0, 1, 2);
    optGrid->addWidget(m_optInvertAlpha, 4, 2);
    optGrid->addWidget(m_optBasicUnsupported, 4, 3);
    root->addWidget(optBox);

    // ── Status + actions ──
    auto* bottom = new QHBoxLayout();
    m_status = new QLabel(this);
    m_status->setStyleSheet(QStringLiteral("color: %1;").arg(t->colorTextSecondary.name()));
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    m_importBtn = new QPushButton(tr("Import Selected Brushes"), this);
    m_importBtn->setDefault(true);
    bottom->addWidget(m_status, 1);
    bottom->addWidget(m_cancelBtn);
    bottom->addWidget(m_importBtn);
    root->addLayout(bottom);

    // ── Wiring ──
    connect(m_addBtn, &QPushButton::clicked, this, [this]() {
        const QString filter = m_importManager ? m_importManager->openFileFilter()
                                                : tr("All files (*)");
        const QStringList paths = QFileDialog::getOpenFileNames(
            this, tr("Add Brush Files"), QString(), filter);
        if (!paths.isEmpty()) addFiles(paths);
    });
    connect(m_clearBtn, &QPushButton::clicked, this, [this]() {
        m_files.clear();
        rebuildFileList();
        rebuildPresetGrid();
        updateStatus();
    });
    connect(m_removeBtn, &QPushButton::clicked, this, [this]() {
        const int idx = currentFileIndex();
        if (idx >= 0 && idx < m_files.size()) {
            m_files.removeAt(idx);
            rebuildFileList();
            rebuildPresetGrid();
            updateStatus();
        }
    });
    connect(m_fileList, &QListWidget::currentRowChanged, this, [this](int) {
        rebuildPresetGrid();
    });
    connect(m_presetGrid, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (DialogPreset* p = presetForItem(item)) {
            p->selected = (item->checkState() == Qt::Checked);
            updateStatus();
        }
    });
    connect(m_presetGrid, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem*, QListWidgetItem*) { updateCompatibilityPanel(); });
    connect(m_search, &QLineEdit::textChanged, this, [this]() { applyFilterAndSearch(); });
    connect(m_filter, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]() { applyFilterAndSearch(); });
    connect(m_selAll, &QPushButton::clicked, this, [this]() { setSelectionForVisible(true, false); });
    connect(m_selNone, &QPushButton::clicked, this, [this]() { setSelectionForVisible(false, false); });
    connect(m_selCompatible, &QPushButton::clicked, this, [this]() { setSelectionForVisible(true, true); });
    connect(m_optInvertAlpha, &QCheckBox::toggled, this, [this]() { onInvertAlphaToggled(); });
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_importBtn, &QPushButton::clicked, this, &BrushImportDialog::doImport);
}

int BrushImportDialog::currentFileIndex() const
{
    return m_fileList ? m_fileList->currentRow() : -1;
}

BrushImportDialog::DialogPreset* BrushImportDialog::presetForItem(QListWidgetItem* item)
{
    if (!item) return nullptr;
    const QPoint id = item->data(Qt::UserRole).toPoint();
    const int fi = id.x();
    const int pi = id.y();
    if (fi < 0 || fi >= m_files.size()) return nullptr;
    if (pi < 0 || pi >= m_files[fi].presets.size()) return nullptr;
    return &m_files[fi].presets[pi];
}

BrushImportOptions BrushImportDialog::currentOptions() const
{
    BrushImportOptions o;
    switch (m_destination->currentIndex()) {
    case 1: o.destination = BrushImportOptions::Destination::CurrentSession; break;
    case 2: o.destination = BrushImportOptions::Destination::CurrentDocument; break;
    default: o.destination = BrushImportOptions::Destination::BrushLibrary; break;
    }
    o.createFolderFromFileName = m_groupFromFile->isChecked();
    o.targetGroupName = m_groupName->text().trimmed();
    o.preserveExternalFolders = m_preserveGroups->isChecked();
    o.preserveExternalGroups = m_preserveGroups->isChecked();
    o.importCompatibleSettings = m_optSettings->isChecked();
    o.importBrushTips = m_optTips->isChecked();
    o.importTexturesWhenPossible = m_optTextures->isChecked();
    o.generatePreviews = m_optPreviews->isChecked();
    o.showCompatibilityWarnings = m_optWarnings->isChecked();
    o.invertImportedAlpha = m_optInvertAlpha->isChecked();
    o.createBasicForUnsupported = m_optBasicUnsupported->isChecked();
    switch (m_duplicates->currentIndex()) {
    case 1: o.duplicatePolicy = BrushImportOptions::DuplicatePolicy::Replace; break;
    case 2: o.duplicatePolicy = BrushImportOptions::DuplicatePolicy::Skip; break;
    case 3: o.duplicatePolicy = BrushImportOptions::DuplicatePolicy::Ask; break;
    default: o.duplicatePolicy = BrushImportOptions::DuplicatePolicy::Rename; break;
    }
    return o;
}

void BrushImportDialog::addFiles(const QStringList& paths)
{
    if (paths.isEmpty()) return;
    if (m_scanning) {
        m_pendingPaths += paths;
        return;
    }
    // Skip files already loaded.
    QStringList fresh;
    for (const QString& p : paths) {
        bool dup = false;
        for (const DialogFile& f : m_files)
            if (f.path == p) { dup = true; break; }
        if (!dup) fresh << p;
    }
    if (fresh.isEmpty()) return;
    startScan(fresh);
}

void BrushImportDialog::onInvertAlphaToggled()
{
    if (m_scanning) return; // a scan is already in flight; ignore the toggle
    // Re-import only image files, since alpha inversion is applied at decode time.
    QStringList imagePaths;
    for (const DialogFile& f : m_files)
        if (f.package.sourceType == BrushImportSourceType::ImageBrushTip)
            imagePaths << f.path;
    if (imagePaths.isEmpty())
        return;
    for (const QString& p : imagePaths)
        for (int i = m_files.size() - 1; i >= 0; --i)
            if (m_files[i].path == p) m_files.removeAt(i);
    rebuildFileList();
    rebuildPresetGrid();
    startScan(imagePaths);
}

void BrushImportDialog::startScan(const QStringList& paths)
{
    if (!m_importManager) return;
    m_scanning = true;
    m_addBtn->setEnabled(false);
    m_importBtn->setEnabled(false);
    m_status->setText(tr("Scanning brushes…"));

    BrushImportManager* mgr = m_importManager;
    const BrushImportOptions options = currentOptions();
    const qreal dpr = devicePixelRatioF();
    const QColor ink = ThemeManager::instance()->current()->colorTextPrimary;
    const bool previews = options.generatePreviews;
    const QSize thumb(kThumbW, kThumbH);

    m_watcher = new QFutureWatcher<QList<DialogFile>>(this);
    connect(m_watcher, &QFutureWatcher<QList<DialogFile>>::finished, this,
            &BrushImportDialog::onScanFinished);

    m_watcher->setFuture(QtConcurrent::run([mgr, paths, options, dpr, ink, previews, thumb]() {
        QList<DialogFile> out;
        BrushPresetConverter converter(5000.0f);
        for (const QString& path : paths) {
            DialogFile df;
            df.path = path;
            df.package = mgr->importFile(path, options);
            for (const ImportedBrushPreset& ip : df.package.presets) {
                DialogPreset dp;
                dp.imported = ip;
                dp.converted = converter.convert(ip, options);
                if (ip.hasError()) {
                    dp.selected = false;
                    ++df.failed;
                } else if (ip.isPartial() || ip.compatibilityLabel == QLatin1String("Partial")
                           || ip.compatibilityLabel == QLatin1String("Unsupported")) {
                    ++df.partial;
                } else {
                    ++df.compatible;
                }
                if (previews && !ip.hasError())
                    dp.thumb = BrushPreviewRenderer::renderStroke(dp.converted.settings, thumb, dpr, ink);
                df.presets.append(dp);
            }
            out.append(df);
        }
        return out;
    }));
}

void BrushImportDialog::onScanFinished()
{
    if (m_watcher) {
        m_files += m_watcher->result();
        m_watcher->deleteLater();
        m_watcher = nullptr;
    }
    m_scanning = false;
    m_addBtn->setEnabled(true);
    m_importBtn->setEnabled(true);

    rebuildFileList();
    if (m_fileList->count() > 0 && m_fileList->currentRow() < 0)
        m_fileList->setCurrentRow(0);
    rebuildPresetGrid();
    updateStatus();

    if (!m_pendingPaths.isEmpty()) {
        const QStringList pending = m_pendingPaths;
        m_pendingPaths.clear();
        addFiles(pending);
    }
}

void BrushImportDialog::rebuildFileList()
{
    const Theme* t = ThemeManager::instance()->current();
    m_fileList->clear();
    for (const DialogFile& f : m_files) {
        const QString label = brushImportSourceLabel(f.package.sourceType);
        const int n = f.presets.size();
        QString line2;
        if (f.package.sourceType == BrushImportSourceType::Unknown || n == 0)
            line2 = tr("Could not be read");
        else
            line2 = tr("%n preset(s)", nullptr, n)
                    + QStringLiteral(" · %1 ok · %2 partial").arg(f.compatible).arg(f.partial);
        auto* item = new QListWidgetItem(
            QStringLiteral("%1\n%2\n%3").arg(QFileInfo(f.path).fileName(), label, line2));
        const bool bad = (n == 0) || f.package.sourceType == BrushImportSourceType::Unknown;
        item->setForeground(QColor(bad ? t->colorDanger : t->colorTextPrimary));
        m_fileList->addItem(item);
    }
}

void BrushImportDialog::rebuildPresetGrid()
{
    m_presetGrid->blockSignals(true);
    m_presetGrid->clear();
    const int fi = currentFileIndex();
    if (fi >= 0 && fi < m_files.size()) {
        const DialogFile& f = m_files[fi];
        for (int pi = 0; pi < f.presets.size(); ++pi) {
            const DialogPreset& p = f.presets[pi];
            auto* item = new QListWidgetItem();
            item->setText(p.converted.name);
            if (!p.thumb.isNull())
                item->setIcon(QIcon(QPixmap::fromImage(p.thumb)));
            QString tag = p.imported.compatibilityLabel.isEmpty()
                              ? tr("Compatible") : p.imported.compatibilityLabel;
            item->setToolTip(QStringLiteral("%1 — %2").arg(p.converted.name, tag));
            Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
            if (!presetFailed(p)) flags |= Qt::ItemIsUserCheckable;
            item->setFlags(flags);
            item->setCheckState(presetFailed(p) ? Qt::Unchecked
                                                 : (p.selected ? Qt::Checked : Qt::Unchecked));
            item->setData(Qt::UserRole, QPoint(fi, pi));
            item->setSizeHint(QSize(kThumbW + 24, kThumbH + 36));
            m_presetGrid->addItem(item);
        }
    }
    m_presetGrid->blockSignals(false);
    applyFilterAndSearch();
    updateCompatibilityPanel();
}

void BrushImportDialog::applyFilterAndSearch()
{
    const QString needle = m_search->text().trimmed();
    const QString filter = m_filter->currentText();
    for (int i = 0; i < m_presetGrid->count(); ++i) {
        QListWidgetItem* item = m_presetGrid->item(i);
        DialogPreset* p = presetForItem(item);
        if (!p) continue;
        bool visible = true;
        if (!needle.isEmpty()) {
            const QString hay = p->converted.name + QLatin1Char(' ')
                                + p->converted.group + QLatin1Char(' ')
                                + p->imported.originalEngineName;
            visible = hay.contains(needle, Qt::CaseInsensitive);
        }
        if (visible && filter != tr("All")) {
            if (filter == tr("Compatible")) visible = !presetPartial(*p) && !presetFailed(*p);
            else if (filter == tr("Partial")) visible = presetPartial(*p) && !presetFailed(*p);
            else if (filter == tr("Unsupported"))
                visible = p->imported.compatibilityLabel == QLatin1String("Unsupported");
            else if (filter == tr("Selected")) visible = p->selected;
            else if (filter == tr("Krita"))
                visible = p->imported.sourceType == BrushImportSourceType::KritaKpp
                          || p->imported.sourceType == BrushImportSourceType::KritaBundle;
        }
        item->setHidden(!visible);
    }
}

void BrushImportDialog::setSelectionForVisible(bool selected, bool compatibleOnly)
{
    for (int i = 0; i < m_presetGrid->count(); ++i) {
        QListWidgetItem* item = m_presetGrid->item(i);
        if (item->isHidden()) continue;
        DialogPreset* p = presetForItem(item);
        if (!p || presetFailed(*p)) continue;
        if (compatibleOnly && presetPartial(*p)) continue;
        p->selected = selected;
        item->setCheckState(selected ? Qt::Checked : Qt::Unchecked);
    }
    updateStatus();
}

void BrushImportDialog::updateCompatibilityPanel()
{
    const Theme* t = ThemeManager::instance()->current();
    DialogPreset* p = presetForItem(m_presetGrid->currentItem());
    if (!p) {
        m_compat->setHtml(QStringLiteral("<i>%1</i>")
                              .arg(tr("Select a brush to see its compatibility.")));
        return;
    }
    const ImportedBrushPreset& ip = p->imported;
    QString html;
    html += QStringLiteral("<b>%1</b><br>").arg(p->converted.name.toHtmlEscaped());
    html += tr("Source: %1<br>").arg(brushImportSourceLabel(ip.sourceType));
    QString status = ip.compatibilityLabel.isEmpty() ? tr("Compatible") : ip.compatibilityLabel;
    html += tr("Status: %1<br>").arg(status);
    html += QStringLiteral("<br>%1<br>").arg(tr("Imported:"));
    html += QStringLiteral("✓ %1<br>").arg(tr("Brush tip"));
    html += QStringLiteral("✓ %1, %2, %3, %4<br>")
                .arg(tr("Size"), tr("Spacing"), tr("Opacity"), tr("Flow"));
    // Pressure is reported when any of the converted size/opacity/flow options
    // carries an active Pressure sensor (the structural import path), or when a
    // simple source asked for the default pressure→size response.
    if (ip.dynamics.pressureAffectsSize
        || brushPressureEnabled(p->converted.settings))
        html += QStringLiteral("✓ %1<br>").arg(tr("Pressure"));
    bool anyIgnored = false;
    QString ignored;
    for (const BrushImportDiagnostic& d : ip.diagnostics) {
        if (d.severity == BrushImportSeverity::Info) continue;
        anyIgnored = true;
        ignored += QStringLiteral("<span style='color:%1'>! %2</span><br>")
                       .arg(sevColor(t, d.severity), d.message.toHtmlEscaped());
    }
    if (anyIgnored)
        html += QStringLiteral("<br>%1<br>%2").arg(tr("Ignored / approximated:"), ignored);
    m_compat->setHtml(html);
}

void BrushImportDialog::updateStatus()
{
    int total = 0, selected = 0, compatible = 0, partial = 0, failed = 0;
    for (const DialogFile& f : m_files) {
        for (const DialogPreset& p : f.presets) {
            ++total;
            if (presetFailed(p)) ++failed;
            else if (presetPartial(p)) ++partial;
            else ++compatible;
            if (p.selected && !presetFailed(p)) ++selected;
        }
    }
    m_status->setText(tr("%1 brushes found, %2 compatible, %3 partial · %4 selected")
                          .arg(total).arg(compatible).arg(partial).arg(selected));
    m_importBtn->setEnabled(selected > 0 && !m_scanning);
}

void BrushImportDialog::doImport()
{
    if (!m_presetManager) { reject(); return; }
    const BrushImportOptions options = currentOptions();
    const bool persist = (options.destination != BrushImportOptions::Destination::CurrentSession);

    std::vector<BrushPreset> toAdd;
    QSet<QString> usedNames;
    QSet<QString> expandedGroups;
    QStringList importedNames;
    QString report;
    int imported = 0, partialCount = 0, skipped = 0, failedCount = 0;

    auto isDup = [&](const QString& name) {
        return usedNames.contains(name) || (m_presetManager->contains(name));
    };
    auto uniqueWithin = [&](const QString& base) {
        if (!isDup(base)) return base;
        for (int i = 2; i < 100000; ++i) {
            const QString c = QStringLiteral("%1 %2").arg(base).arg(i);
            if (!isDup(c)) return c;
        }
        return base;
    };

    for (const DialogFile& f : m_files) {
        for (const DialogPreset& dp : f.presets) {
            if (!dp.selected || presetFailed(dp)) {
                if (presetFailed(dp)) ++failedCount;
                continue;
            }
            BrushPreset bp = dp.converted;
            bp.group = BrushPresetConverter::groupPathFor(dp.imported, options);

            const QString baseName = bp.name.isEmpty() ? tr("Unnamed Brush") : bp.name;
            switch (options.duplicatePolicy) {
            case BrushImportOptions::DuplicatePolicy::Skip:
                if (isDup(baseName)) { ++skipped; continue; }
                bp.name = baseName;
                break;
            case BrushImportOptions::DuplicatePolicy::Replace:
                bp.name = baseName; // addPresets replaces same-name
                break;
            case BrushImportOptions::DuplicatePolicy::Rename:
            case BrushImportOptions::DuplicatePolicy::Ask: // Ask falls back to rename
            default:
                bp.name = uniqueWithin(baseName);
                break;
            }

            usedNames.insert(bp.name);
            if (!bp.group.isEmpty()) expandedGroups.insert(bp.group);
            toAdd.push_back(bp);
            importedNames << bp.name;
            ++imported;
            if (presetPartial(dp)) ++partialCount;
        }
        // Build the report section for this file.
        for (const BrushImportDiagnostic& d : f.package.diagnostics) {
            if (d.severity == BrushImportSeverity::Info) continue;
            report += QStringLiteral("%1: %2\n").arg(QFileInfo(f.path).fileName(), d.message);
        }
    }

    if (toAdd.empty()) {
        QMessageBox::information(this, tr("Import Brushes"),
                                 tr("No brushes were imported."));
        return;
    }

    m_presetManager->addPresets(toAdd, persist);
    emit brushesImported(importedNames, QStringList(expandedGroups.begin(), expandedGroups.end()));

    // Honest summary; offer the full diagnostics report on demand.
    QString summary = tr("%1 brushes imported into the brush library.").arg(imported);
    if (!persist)
        summary = tr("%1 brushes imported for this session.").arg(imported);
    if (partialCount > 0)
        summary += QLatin1Char('\n') + tr("%1 brushes were partially imported.").arg(partialCount);
    if (skipped > 0)
        summary += QLatin1Char('\n') + tr("%1 duplicates were skipped.").arg(skipped);
    if (failedCount > 0)
        summary += QLatin1Char('\n') + tr("%1 brushes could not be imported.").arg(failedCount);

    if (options.showCompatibilityWarnings && !report.isEmpty()) {
        QMessageBox box(QMessageBox::Information, tr("Brushes Imported"), summary,
                        QMessageBox::Ok, this);
        box.setDetailedText(report);
        box.exec();
    } else {
        QMessageBox::information(this, tr("Brushes Imported"), summary);
    }
    accept();
}
