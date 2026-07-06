#pragma once

#include <QDialog>
#include <QList>
#include <QImage>
#include "brush/import/BrushImportTypes.hpp"
#include "brush/BrushPreset.hpp"

class BrushImportManager;
class BrushPresetManager;
class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QTextBrowser;
template <typename T> class QFutureWatcher;

// Brush import window: add multiple files, preview the brushes
// each contains, choose which to import, see per-brush compatibility, pick the
// destination/grouping, then commit into the brush library. Scanning/importing
// runs on a worker thread so the UI never blocks; previews are rendered through
// the real brush preview pipeline (BrushPreviewRenderer), not faked.
class BrushImportDialog : public QDialog {
    Q_OBJECT

public:
    BrushImportDialog(BrushImportManager* importManager,
                      BrushPresetManager* presetManager,
                      QWidget* parent = nullptr);
    ~BrushImportDialog() override;

    // Queue files to import (used by drag & drop / menu entry points). Safe to
    // call before or after show().
    void addFiles(const QStringList& paths);

signals:
    // Emitted after a successful commit so the host can refresh the Brush Panel,
    // select the first imported brush and show a summary.
    void brushesImported(const QStringList& presetNames, const QStringList& expandedGroups);

private:
    struct DialogPreset {
        ImportedBrushPreset imported;
        BrushPreset converted;   // engine preset (group re-derived at commit)
        QImage thumb;
        bool selected = true;
    };
    struct DialogFile {
        QString path;
        ImportedBrushPackage package;
        QList<DialogPreset> presets;
        int compatible = 0;
        int partial = 0;
        int failed = 0;
    };

    void buildUi();
    BrushImportOptions currentOptions() const;
    void startScan(const QStringList& paths);
    void onScanFinished();
    void rebuildFileList();
    void rebuildPresetGrid();
    void updateCompatibilityPanel();
    void updateStatus();
    void applyFilterAndSearch();
    void setSelectionForVisible(bool selected, bool compatibleOnly);
    void doImport();
    void onInvertAlphaToggled();

    DialogPreset* presetForItem(QListWidgetItem* item);
    int currentFileIndex() const;

    static bool presetFailed(const DialogPreset& p);
    static bool presetPartial(const DialogPreset& p);

    BrushImportManager* m_importManager = nullptr;
    BrushPresetManager* m_presetManager = nullptr;

    QList<DialogFile> m_files;
    QStringList m_pendingPaths;        // queued before the dialog is ready
    bool m_scanning = false;

    // ── widgets ──
    QPushButton* m_addBtn = nullptr;
    QPushButton* m_removeBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QListWidget* m_fileList = nullptr;
    QListWidget* m_presetGrid = nullptr;
    QLineEdit* m_search = nullptr;
    QComboBox* m_filter = nullptr;
    QPushButton* m_selAll = nullptr;
    QPushButton* m_selNone = nullptr;
    QPushButton* m_selCompatible = nullptr;
    QTextBrowser* m_compat = nullptr;

    QComboBox* m_destination = nullptr;
    QCheckBox* m_groupFromFile = nullptr;
    QLineEdit* m_groupName = nullptr;
    QCheckBox* m_preserveGroups = nullptr;
    QComboBox* m_duplicates = nullptr;

    QCheckBox* m_optSettings = nullptr;
    QCheckBox* m_optTips = nullptr;
    QCheckBox* m_optTextures = nullptr;
    QCheckBox* m_optPreviews = nullptr;
    QCheckBox* m_optWarnings = nullptr;
    QCheckBox* m_optInvertAlpha = nullptr;
    QCheckBox* m_optBasicUnsupported = nullptr;

    QLabel* m_status = nullptr;
    QPushButton* m_importBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;

    QFutureWatcher<QList<DialogFile>>* m_watcher = nullptr;
};
