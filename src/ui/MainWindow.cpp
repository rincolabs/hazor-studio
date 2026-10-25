#include "MainWindow.hpp"
#include <QAbstractSpinBox>
#include "core/AdjustmentTypes.hpp"
#include "core/AppPaths.hpp"
#include "core/CrashHandler.hpp"
#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "transform/TransformController.hpp"
#include "core/ViewportCamera.hpp"
#include "renderer/CanvasView.hpp"
#include "ui/AppMessageDialog.hpp"
#include "ui/LayerPanel.hpp"
#include "ui/ChannelsPanel.hpp"
#include "ui/dialogs/ChannelDialogs.hpp"
#include "ui/LayerStylesDialog.hpp"
#include "ui/PropertiesPanel.hpp"
#include "ui/CurvesEditorWidget.hpp"
#include "ui/HueSaturationAdjustmentWidget.hpp"
#include "ui/HistogramPanel.hpp"
#include "ui/HistoryPanel.hpp"
#include "ui/LayerPanel.hpp"
#include "ui/SwatchesPanel.hpp"
#include "ui/ColorMixerPanel.hpp"
#include "ui/ColorPaletteBar.hpp"
#include "ui/ToolOptionsBar.hpp"
#include "ui/AlignBar.hpp"
#include "ui/IconUtils.hpp"
#include "ui/ToolsPanel.hpp"
#include "ui/BrushDynamicsPanel.hpp"
#include "ui/BrushPanel.hpp"
#include "ui/brush/BrushImportDialog.hpp"
#include "brush/import/BrushImportManager.hpp"
#include "brush/BrushPresetManager.hpp"
#include "ui/CustomShapesPanel.hpp"
#include "ui/TitleBar.hpp"
#include "ui/HomeWorkspace.hpp"
#include "ui/NewDocumentDialog.hpp"
#include "ui/ExportImageDialog.hpp"
#include "ui/RawDevelopWindow.hpp"
#include "ui/AgentConfigWidget.hpp"
#include "ui/AiUpscaleDialog.hpp"
#include "ui/FillLayerDialog.hpp"
#include "ui/ResizeLayerDialog.hpp"
#include "ui/dialogs/AdjustColorDialog.hpp"
#include "ui/dialogs/GaussianBlurDialog.hpp"
#include "ui/dialogs/SharpenDialog.hpp"
#include "ui/dialogs/MedianBlurDialog.hpp"
#include "ui/dialogs/EdgeDetectDialog.hpp"
#include "ui/dialogs/PosterizeDialog.hpp"
#include "ui/dialogs/ThresholdDialog.hpp"
#include "ui/dialogs/NoiseReduceDialog.hpp"
#include "ui/dialogs/RemoveBgDialog.hpp"
#include "ui/ShortcutManager.hpp"
#include "ui/AppSettingsDialog.hpp"
#include "mcp/McpServer.hpp"
#include "mcp/McpSettings.hpp"
#include "ui/AiAlertService.hpp"
#include "ai/tool/AiObjectSelectionController.hpp"
#include "ai/tool/AiRemoveObjectController.hpp"
#include "ai/compat/AiCompatibilityManager.hpp"
#include "ai/compat/AiModelCompatibilityChecker.hpp"
#include "ai/models/AiModelRegistry.hpp"
#include "ai/runtime/AiRuntimeManager.hpp"
#include "ai/runtime/AiRuntimeSettings.hpp"
#include "ai/runtime/AiExecutionProvider.hpp"
#include "ui/GenerativeFillDialog.hpp"
#include "ui/ProgressDialog.h"
#include "ui/ZoomComboBox.hpp"
#include <QSettings>
#include "ui/dialogs/ToolDialog.hpp"
#include "ui/RefineEdgeDialog.hpp"
#include "controller/Commands.hpp"
#include "core/ColorEngine.hpp"
#include "core/SwatchManager.hpp"
#include "core/GuideTypes.hpp"
#include "colorpicker/ColorPickerDialog.hpp"
#include "colorpicker/CanvasColorPickSource.hpp"

#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "core/LayerEffect.hpp"
#include "io/ImageIO.hpp"
#include "io/ProjectFileService.hpp"
#include "smartobject/ContainerRegistry.hpp"
#include "smartobject/DocumentContainer.hpp"
#include "smartobject/SmartObjectTransfer.hpp"
#include "io/RecentFilesManager.hpp"
#include "io/PsdImportService.hpp"
#include "color/ColorManagementService.hpp"
#include "color/ColorProfile.hpp"
#include "raw/RawDevelopParams.hpp"
#include "raw/RawDevelopService.hpp"
#include "raw/RawEngine.hpp"
#include "raw/RawImageBuffer.hpp"
#include "raw/RawLayerData.hpp"
#include "raw/RawSession.hpp"
#include "ui/color/AssignProfileDialog.hpp"
#include "ui/color/ConvertToProfileDialog.hpp"
#include "ui/color/MissingProfileDialog.hpp"
#include "ui/color/ProfileMismatchDialog.hpp"
#include "ui/color/SoftProofDialog.hpp"

#include "controller/ImageController.hpp"
#include "tools/ToolExecutor.hpp"
#include "agent/AgentController.hpp"
#include "agent/AgentPanel.hpp"
#include "agent/AgentPresetManager.hpp"

#include <QMenuBar>
#include <QMenu>
#include <QInputDialog>
#include <QShortcut>
#include <QSignalBlocker>
#include <QApplication>
#include <QCloseEvent>
#include <QLineEdit>
#include "shape/ShapePresetFactory.hpp"
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QToolBar>
#include <QDebug>
#include <QDockWidget>
#include <QHash>
#include <QPointer>
#include <QChildEvent>
#include <QFileDialog>
#include <QDesktopServices>
#include <QFontDialog>
#include <QMessageBox>
#include <QOpenGLWidget>
#include <QPushButton>
#include <QLabel>
#include <QPainter>
#include <QImage>
#include <QPixmap>
#include <QGuiApplication>
#include <QWindow>
#include <QResizeEvent>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSizePolicy>
#include <QTimer>
#include <QMimeData>
#include <QUrl>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QFile>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <QDockWidget>
#include <QTabBar>

static QString formattedZoomPercent(float zoom)
{
    const double percent = std::max(0.0, static_cast<double>(zoom) * 100.0);
    QString value = QString::number(percent, 'f', 1);
    if (value.endsWith(QStringLiteral(".0")))
        value.chop(2);
    return value;
}

static QString documentColorModeLabel(const QString& colorMode)
{
    if (colorMode == QStringLiteral("RGB Color"))
        return QStringLiteral("RGB");
    if (colorMode == QStringLiteral("CMYK Color"))
        return QStringLiteral("CMYK");
    if (colorMode == QStringLiteral("Grayscale"))
        return QStringLiteral("Gray");

    QString label = colorMode.trimmed();
    if (label.endsWith(QStringLiteral(" Color"), Qt::CaseInsensitive))
        label.chop(6);
    return label.isEmpty() ? QStringLiteral("RGB") : label;
}

static QString documentColorModeFromSetting(const QString& setting)
{
    if (setting.startsWith(QStringLiteral("CMYK"), Qt::CaseInsensitive))
        return QStringLiteral("CMYK Color");
    if (setting.startsWith(QStringLiteral("Grayscale"), Qt::CaseInsensitive))
        return QStringLiteral("Grayscale");
    return QStringLiteral("RGB Color");
}

static int documentBitDepthFromSetting(const QString& setting)
{
    return setting.contains(QStringLiteral("16")) ? 16 : 8;
}

static QString documentColorModeSetting(const QString& colorMode, int bitDepth)
{
    return QStringLiteral("%1, %2 bit").arg(colorMode).arg(bitDepth);
}

static int shapeToolTypeForPreset(const QString& presetId)
{
    if (presetId == QLatin1String("ellipse")) return 1;
    if (presetId == QLatin1String("line")) return 2;
    if (presetId == QLatin1String("polygon")) return 3;
    if (presetId == QLatin1String("arrow")) return 4;
    if (presetId == QLatin1String("star")) return 5;
    if (presetId == QLatin1String("custom-svg-icon")) return 6;
    return 0;
}

static bool isSupportedDropImagePath(const QString& path)
{
    // .hazor isn't an image codec format — it's the project container, opened
    // via loadProjectAsNewTab() — so it must be special-cased here or a
    // dragged project is silently rejected before the drop even fires. .hzs
    // is the old extension, still readable, so a dragged legacy project
    // isn't rejected either.
    return imageCodecRegistry().findReader(path) != nullptr
        || path.endsWith(QStringLiteral(".hazor"), Qt::CaseInsensitive)
        || path.endsWith(QStringLiteral(".hzs"), Qt::CaseInsensitive);
}

static QStringList extractValidDropImagePaths(const QMimeData* mime)
{
    QStringList out;
    if (!mime || !mime->hasUrls()) return out;
    for (const QUrl& u : mime->urls()) {
        if (!u.isLocalFile()) continue;
        const QString path = u.toLocalFile();
        if (isSupportedDropImagePath(path))
            out.push_back(path);
    }
    return out;
}

constexpr auto kColorGroup = "colorManagement";

static MissingProfilePolicy policyFromChoice(MissingProfileUserChoice choice)
{
    switch (choice) {
    case MissingProfileUserChoice::LeaveUntagged:
        return MissingProfilePolicy::LeaveUntagged;
    case MissingProfileUserChoice::AssignWorking:
        return MissingProfilePolicy::AssignWorkingSpace;
    case MissingProfileUserChoice::AssignSRgb:
        return MissingProfilePolicy::AssumeSRgb;
    }
    return MissingProfilePolicy::AssumeSRgb;
}

static ProfileMismatchPolicy policyFromChoice(ProfileMismatchUserChoice choice)
{
    switch (choice) {
    case ProfileMismatchUserChoice::PreserveEmbedded:
        return ProfileMismatchPolicy::PreserveEmbeddedProfile;
    case ProfileMismatchUserChoice::ConvertToWorking:
        return ProfileMismatchPolicy::ConvertToWorkingSpace;
    case ProfileMismatchUserChoice::AssignWorking:
        return ProfileMismatchPolicy::AssignWorkingSpace;
    }
    return ProfileMismatchPolicy::PreserveEmbeddedProfile;
}

static ColorProfile profileAfterMissingPolicy(const DocumentImage& image,
                                              const OpenColorPolicyContext& context)
{
    if (image.colorProfile.isValid())
        return image.colorProfile;

    switch (context.missingProfilePolicy) {
    case MissingProfilePolicy::AssumeSRgb:
        return ColorProfile::sRgb();
    case MissingProfilePolicy::AssignWorkingSpace:
        return context.workingSpace.isValid() ? context.workingSpace : ColorProfile::sRgb();
    case MissingProfilePolicy::AskUser:
    case MissingProfilePolicy::LeaveUntagged:
        break;
    }
    return ColorProfile::invalid();
}

static void persistMissingPolicy(MissingProfilePolicy policy)
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kColorGroup));
    settings.setValue(QStringLiteral("missingProfilePolicy"), static_cast<int>(policy));
    settings.endGroup();
    ColorManagementService::instance().reloadSettings();
}

static void persistMismatchPolicy(ProfileMismatchPolicy policy)
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kColorGroup));
    settings.setValue(QStringLiteral("profileMismatchPolicy"), static_cast<int>(policy));
    settings.endGroup();
    ColorManagementService::instance().reloadSettings();
}

static std::optional<ColorManagedOpenResult> prepareImageForOpenWithDialogs(
    QWidget* parent,
    const DocumentImage& image,
    OpenColorPolicyContext context)
{
    if (!context.workingSpace.isValid())
        context.workingSpace = ColorProfile::sRgb();

    if (!image.colorProfile.isValid()
        && context.missingProfilePolicy == MissingProfilePolicy::AskUser) {
        MissingProfileDialog dialog(context.workingSpace, parent);
        if (dialog.exec() != QDialog::Accepted)
            return std::nullopt;

        context.missingProfilePolicy = policyFromChoice(dialog.choice());
        if (dialog.dontAskAgain())
            persistMissingPolicy(context.missingProfilePolicy);
    }

    const ColorProfile mismatchSource = profileAfterMissingPolicy(image, context);
    if (mismatchSource.isValid()
        && context.workingSpace.isValid()
        && !mismatchSource.equivalentTo(context.workingSpace)
        && context.mismatchPolicy == ProfileMismatchPolicy::AskUser) {
        ProfileMismatchDialog dialog(mismatchSource, context.workingSpace, parent);
        if (dialog.exec() != QDialog::Accepted)
            return std::nullopt;

        context.mismatchPolicy = policyFromChoice(dialog.choice());
        if (dialog.dontAskAgain())
            persistMismatchPolicy(context.mismatchPolicy);
    }

    ColorManagedOpenResult result =
        ColorManagementService::instance().prepareImageForOpen(image, context);

    const QString iccWarning = image.metadata.value(QStringLiteral("iccWarning")).toString();
    if (!iccWarning.isEmpty()) {
        result.warningMessage = result.warningMessage.isEmpty()
            ? iccWarning
            : QStringLiteral("%1 %2").arg(result.warningMessage, iccWarning);
    }
    return result;
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    resize(1400, 850);
    setWindowTitle(tr("Hazor Studio"));
    setAcceptDrops(true);

    m_toolExecutor = new ToolExecutor(nullptr);
    m_colorEngine = new ColorEngine(this);

    // The colour dialogs are opened from panels, tool options and adjustment
    // editors, none of which knows about documents or tabs. Give them one way to
    // reach the canvas the user is looking at: the resolver is consulted per
    // call, so it keeps up with every tab switch on its own. The QPointer keeps
    // a dialog that outlives this window from calling into freed memory.
    CanvasColorPickSource::setResolver([self = QPointer<MainWindow>(this)]() -> CanvasView* {
        return self ? self->m_canvas : nullptr;
    });

    m_agentController = new AgentController(m_toolExecutor, this);
    m_presetManager = new AgentPresetManager(this);

    m_useCustomTitleBar = TitleBar::isSupportedPlatform();

    if (m_useCustomTitleBar) {
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);

        m_titleBar = new TitleBar(this);
        m_titleBar->setGeometry(0, 0, width(), TitleBar::kBarHeight);
        setContentsMargins(0, TitleBar::kBarHeight, 0, 0);

        connect(m_titleBar, &TitleBar::minimizeClicked, this, &MainWindow::onMinimize);
        connect(m_titleBar, &TitleBar::maximizeClicked, this, &MainWindow::onMaximizeRestore);
        connect(m_titleBar, &TitleBar::closeClicked, this, &MainWindow::onCloseWindow);
    }

    // Qt 6.4+ recreates an already-visible native window when its first
    // QOpenGLWidget is added because the surface changes from RasterSurface to
    // OpenGLSurface. Home deliberately starts without a document/CanvasView,
    // so prime the widget hierarchy before MainWindow is shown. This hidden
    // anchor owns no document and never initializes editor rendering resources.
    auto* openGlSurfaceAnchor = new QOpenGLWidget(this);
    openGlSurfaceAnchor->setObjectName(QStringLiteral("OpenGLSurfaceAnchor"));
    openGlSurfaceAnchor->setFixedSize(0, 0);
    openGlSurfaceAnchor->hide();


    // Build the main menu on a standalone QMenuBar. When the custom title bar is
    // active it is embedded into the title bar (see below); otherwise it falls
    // back to the traditional QMainWindow menu bar. The same instance, actions
    // and shortcuts are used either way — no duplicate menu tree.
    auto* mainMenuBar = new QMenuBar(this);
    mainMenuBar->setStyleSheet(QStringLiteral(
        "QMenuBar { padding-top: 6px; padding-bottom: 4px; }"));

    auto* fileMenu = mainMenuBar->addMenu(tr("&File"));

    auto* newAction = fileMenu->addAction(tr("&New..."));
    newAction->setShortcut(QKeySequence::New);
    ShortcutManager::instance()->registerAction("file.new", newAction);
    connect(newAction, &QAction::triggered, this, &MainWindow::onNewFile);

    auto* openAction = fileMenu->addAction(tr("&Open..."));
    openAction->setShortcut(QKeySequence::Open);
    ShortcutManager::instance()->registerAction("file.open", openAction);
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpenFile);

    m_openRecentMenu = fileMenu->addMenu(tr("Open &Recent"));
    m_openRecentMenu->setToolTipsVisible(true);
    connect(m_openRecentMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildOpenRecentMenu);

    auto* saveAction = fileMenu->addAction(tr("&Save..."));
    saveAction->setShortcut(QKeySequence::Save);
    ShortcutManager::instance()->registerAction("file.save", saveAction);
    connect(saveAction, &QAction::triggered, this, &MainWindow::onSaveFile);

    auto* saveAsAction = fileMenu->addAction(tr("Save &As..."));
    saveAsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    ShortcutManager::instance()->registerAction("file.save_as", saveAsAction);
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::onSaveAsFile);

    auto* exportAction = fileMenu->addAction(tr("&Export As..."));
    exportAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
    ShortcutManager::instance()->registerAction("file.export_as", exportAction);
    connect(exportAction, &QAction::triggered, this, &MainWindow::onExportFile);

    fileMenu->addSeparator();

    // Place lives in File because it brings an external document INTO the
    // current one, which is a file operation, not a layer operation. The
    // Layer ▸ Smart Objects submenu deliberately does not repeat it.
    m_smartObjectPlaceEmbeddedAction = fileMenu->addAction(tr("Place Embedded..."));
    ShortcutManager::instance()->registerAction("file.place_embedded",
                                                m_smartObjectPlaceEmbeddedAction);
    connect(m_smartObjectPlaceEmbeddedAction, &QAction::triggered,
            this, [this]() { placeSmartObject(/*linked=*/false); });

    m_smartObjectPlaceLinkedAction = fileMenu->addAction(tr("Place Linked..."));
    ShortcutManager::instance()->registerAction("file.place_linked",
                                                m_smartObjectPlaceLinkedAction);
    connect(m_smartObjectPlaceLinkedAction, &QAction::triggered,
            this, [this]() { placeSmartObject(/*linked=*/true); });

    fileMenu->addSeparator();

    m_importAction = fileMenu->addAction(tr("&Import Image..."));
    m_importAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
    ShortcutManager::instance()->registerAction("file.import", m_importAction);
    connect(m_importAction, &QAction::triggered, this, &MainWindow::onImportImage);

    m_importBrushesAction = fileMenu->addAction(tr("Import &Brushes..."));
    connect(m_importBrushesAction, &QAction::triggered, this,
            [this]() { openBrushImportDialog(); });

    fileMenu->addSeparator();

    auto* exitAction = fileMenu->addAction(tr("E&xit"));
    exitAction->setShortcut(QKeySequence::Quit);
    ShortcutManager::instance()->registerAction("file.exit", exitAction);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    auto* editMenu = mainMenuBar->addMenu(tr("&Edit"));

    auto* undoAction = editMenu->addAction(tr("&Undo"));
    undoAction->setShortcut(QKeySequence::Undo);
    ShortcutManager::instance()->registerAction("edit.undo", undoAction);
    connect(undoAction, &QAction::triggered, this, &MainWindow::onUndo);

    auto* redoAction = editMenu->addAction(tr("&Redo"));
    redoAction->setShortcut(QKeySequence::Redo);
    ShortcutManager::instance()->registerAction("edit.redo", redoAction);
    connect(redoAction, &QAction::triggered, this, &MainWindow::onRedo);

    auto* copyAction = editMenu->addAction(tr("&Copy"));
    copyAction->setShortcut(QKeySequence::Copy);
    ShortcutManager::instance()->registerAction("edit.copy", copyAction);
    connect(copyAction, &QAction::triggered, this, [this]() {
        if (auto* ctrl = activeController()) ctrl->copy();
    });

    auto* pasteAction = editMenu->addAction(tr("&Paste"));
    pasteAction->setShortcut(QKeySequence::Paste);
    ShortcutManager::instance()->registerAction("edit.paste", pasteAction);
    connect(pasteAction, &QAction::triggered, this, [this]() {
        // paste() resolves the source itself: system clipboard first (external
        // image → new pixel layer), internal rich clipboard only while the
        // system clipboard still carries our own copy marker.
        if (auto* ctrl = activeController())
            ctrl->paste();
    });

    auto* freeTransformAction = editMenu->addAction(tr("Free &Transform"));
    freeTransformAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    ShortcutManager::instance()->registerAction("edit.free_transform", freeTransformAction);
    connect(freeTransformAction, &QAction::triggered, this, [this]() {
        if (auto* canvas = activeCanvas()) {
            if (canvas->isFreeTransformActive())
                canvas->commitFreeTransform();
            else
                canvas->beginFreeTransform();
        }
    });

    editMenu->addSeparator();

    auto* docAction = editMenu->addAction(tr("&Document..."));
    ShortcutManager::instance()->registerAction("edit.document", docAction);
    connect(docAction, &QAction::triggered, this, &MainWindow::onEditDocument);

    auto* colorSettingsAction = editMenu->addAction(tr("Color &Settings..."));
    ShortcutManager::instance()->registerAction("edit.color_settings", colorSettingsAction);
    connect(colorSettingsAction, &QAction::triggered, this, [this]() {
        showAppSettings(AppSettingsDialog::PageColorManagement, [this](int result) {
            if (result != QDialog::Accepted)
                return;
            // Color/display settings (display CM, monitor profile, intent) change
            // how every open document is shown without touching composition —
            // invalidate each document's display cache and repaint its canvas.
            for (auto& tab : m_tabs) {
                if (tab.document)
                    tab.document->invalidateDisplayCache();
                if (tab.canvas)
                    tab.canvas->update();
            }
            updateStatusFromDocument();
        });
    });

    auto* assignProfileAction = editMenu->addAction(tr("Assign &Profile..."));
    ShortcutManager::instance()->registerAction("edit.assign_profile", assignProfileAction);
    connect(assignProfileAction, &QAction::triggered, this, [this]() {
        if (!m_doc || !m_ctrl)
            return;

        AssignProfileDialog dlg(m_doc->colorProfile(), this);
        if (dlg.exec() != QDialog::Accepted)
            return;

        ColorProfile selectedProfile = dlg.selectedProfile();
        if (selectedProfile.equivalentTo(m_doc->colorProfile())
            && selectedProfile.isValid() == m_doc->colorProfile().isValid()) {
            return;
        }

        auto command = std::make_unique<AssignProfileCommand>(m_doc, selectedProfile);
        command->execute();
        m_ctrl->pushCommand(std::move(command));
        updateStatusFromDocument();
        if (m_canvas)
            m_canvas->update();
    });

    auto* convertProfileAction = editMenu->addAction(tr("Convert to &Profile..."));
    ShortcutManager::instance()->registerAction("edit.convert_to_profile", convertProfileAction);
    connect(convertProfileAction, &QAction::triggered, this, [this]() {
        if (!m_doc || !m_ctrl || !m_doc->colorProfile().isValid())
            return;

        ConvertToProfileDialog dlg(m_doc->colorProfile(), this);
        if (dlg.exec() != QDialog::Accepted)
            return;

        const ColorProfile destination = dlg.destinationProfile();
        if (!destination.isValid() || destination.equivalentTo(m_doc->colorProfile()))
            return;

        if (!m_ctrl->convertDocumentToProfile(destination, dlg.conversionOptions())) {
            AppMessageDialog::warning(this, tr("Convert to Profile"),
                tr("The selected color conversion is not supported by the current color engine."));
            return;
        }
        refreshLayerPanel();
        refreshPropertiesPanel();
        updateStatusFromDocument();
        if (m_canvas) {
            m_canvas->syncLayersToGpu();
            m_canvas->update();
        }
    });

    editMenu->addSeparator();

    auto* agentCfgAction = editMenu->addAction(tr("Configure &Agent..."));
    ShortcutManager::instance()->registerAction("edit.configure_agent", agentCfgAction);
    connect(agentCfgAction, &QAction::triggered, this, &MainWindow::onConfigureAgent);

    editMenu->addSeparator();

    auto* settingsAction = editMenu->addAction(tr("&Settings..."));
    settingsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    ShortcutManager::instance()->registerAction("edit.settings", settingsAction);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::onSettings);

    auto* layerMenu = mainMenuBar->addMenu(tr("&Layer"));

    m_addLayerAction = layerMenu->addAction(tr("&New Layer"));
    m_addLayerAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
    ShortcutManager::instance()->registerAction("layer.new", m_addLayerAction);
    connect(m_addLayerAction, &QAction::triggered, this, &MainWindow::onMenuAddLayer);

    m_removeLayerAction = layerMenu->addAction(tr("&Remove Layer"));
    m_removeLayerAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Delete));
    ShortcutManager::instance()->registerAction("layer.remove", m_removeLayerAction);
    connect(m_removeLayerAction, &QAction::triggered, this, &MainWindow::onMenuRemoveLayer);

    m_duplicateLayerAction = layerMenu->addAction(tr("&Duplicate Layer"));
    m_duplicateLayerAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_J));
    ShortcutManager::instance()->registerAction("layer.duplicate", m_duplicateLayerAction);
    connect(m_duplicateLayerAction, &QAction::triggered, this, &MainWindow::onMenuDuplicateLayer);

    layerMenu->addSeparator();

    // Structural actions (Etapa 12): these are QAction-owned shortcuts (like
    // New/Duplicate) so a single, registered action drives both the menu and the
    // key. They are intentionally NOT also registered as dispatcher callbacks —
    // Qt's shortcut path and the qApp key-event filter are independent, so having
    // both would risk double-firing.
    auto addLayerMenuAction = [this](QMenu* menu, const QString& id, const QString& text,
                                     void (MainWindow::*handler)()) {
        auto* action = menu->addAction(text);
        ShortcutManager::instance()->registerAction(id, action);
        connect(action, &QAction::triggered, this, handler);
        return action;
    };
    addLayerMenuAction(layerMenu, QStringLiteral("layer.group.create_from_selection"),
                       tr("&Group Layers"), &MainWindow::groupSelectedLayers);
    addLayerMenuAction(layerMenu, QStringLiteral("layer.group.ungroup"),
                       tr("&Ungroup Layers"), &MainWindow::ungroupSelectedLayers);
    auto* arrangeMenu = layerMenu->addMenu(tr("&Arrange"));
    {
        auto addArrange = [this, arrangeMenu](const QString& id, const QString& text, int kind) {
            auto* action = arrangeMenu->addAction(text);
            ShortcutManager::instance()->registerAction(id, action);
            connect(action, &QAction::triggered, this, [this, kind]() { arrangeActiveLayer(kind); });
        };
        addArrange(QStringLiteral("layer.arrange.to_front"), tr("Bring to &Front"), 2);
        addArrange(QStringLiteral("layer.arrange.forward"),  tr("Bring &Forward"),  0);
        addArrange(QStringLiteral("layer.arrange.backward"), tr("Send &Backward"),  1);
        addArrange(QStringLiteral("layer.arrange.to_back"),  tr("Send to &Back"),   3);
    }
    // One action, label swapped in updateLayerMenuState between "Create" and
    // "Release" — the Layer menu reads as the two commands it is, while the
    // single Ctrl+Alt+G toggle keeps working on whatever the selection is.
    m_clippingMaskAction =
        addLayerMenuAction(layerMenu, QStringLiteral("layer.clipping.toggle"),
                           tr("Create &Clipping Mask"), &MainWindow::toggleActiveClipping);

    // New Adjustment Layer > <adjustment>. Populated from the adjustment
    // registry so new adjustments appear here automatically.
    m_adjustmentLayerMenu = layerMenu->addMenu(tr("New &Adjustment Layer"));
    for (const auto& info : adjustments::registry()) {
        const QString id = info.id;
        QAction* act = m_adjustmentLayerMenu->addAction(info.displayName);
        connect(act, &QAction::triggered, this, [this, id]() {
            if (!m_ctrl) return;
            // Solid Color opens the colour picker first; cancel = no layer.
            if (id == QLatin1String("solidcolor")) {
                createSolidColorLayer();
                return;
            }
            m_ctrl->addAdjustmentLayer(id);
            if (m_canvas) { m_canvas->syncLayersToGpu(); m_canvas->update(); }
            refreshLayerPanel();
            updateLayerMenuState();
        });
    }

    m_fillLayerAction = layerMenu->addAction(tr("&Fill Layer..."));
    m_fillLayerAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Backspace));
    ShortcutManager::instance()->registerAction("layer.fill", m_fillLayerAction);
    connect(m_fillLayerAction, &QAction::triggered, this, &MainWindow::onMenuFillLayer);

    m_mergeVisibleAction = layerMenu->addAction(tr("&Merge Visible"));
    m_mergeVisibleAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
    ShortcutManager::instance()->registerAction("layer.merge_visible", m_mergeVisibleAction);
    connect(m_mergeVisibleAction, &QAction::triggered, this, &MainWindow::onMenuMergeVisible);

    m_mergeDownAction = layerMenu->addAction(tr("Merge &Down"));
    m_mergeDownAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    ShortcutManager::instance()->registerAction("layer.merge_down", m_mergeDownAction);
    connect(m_mergeDownAction, &QAction::triggered, this, &MainWindow::onMenuMergeDown);

    m_mergeLayersAction = layerMenu->addAction(tr("&Merge Layers"));
    ShortcutManager::instance()->registerAction("layer.merge_layers", m_mergeLayersAction);
    connect(m_mergeLayersAction, &QAction::triggered, this, &MainWindow::onMenuMergeLayers);

    m_flattenImageAction = layerMenu->addAction(tr("&Flatten Image"));
    ShortcutManager::instance()->registerAction("layer.flatten_image", m_flattenImageAction);
    connect(m_flattenImageAction, &QAction::triggered, this, &MainWindow::onMenuFlattenImage);

    layerMenu->addSeparator();

    m_rasterizeLayerAction = layerMenu->addAction(tr("Rasterize &Layer"));
    ShortcutManager::instance()->registerAction("layer.rasterize", m_rasterizeLayerAction);
    connect(m_rasterizeLayerAction, &QAction::triggered, this, &MainWindow::onMenuRasterizeLayer);

    // Layer ▸ Smart Objects — the name, order and grouping follow the
    // convention this kind of editor established, so the muscle memory
    // transfers. Place lives in File (above), and Rasterize stays where it is:
    // it is the generic "drop the non-destructive payload" action shared with
    // Text/Shape/RAW.
    m_smartObjectMenu = layerMenu->addMenu(tr("Smart &Objects"));

    m_smartObjectConvertAction = m_smartObjectMenu->addAction(tr("&Convert to Smart Object"));
    ShortcutManager::instance()->registerAction("layer.smart_object.convert",
                                                m_smartObjectConvertAction);
    connect(m_smartObjectConvertAction, &QAction::triggered,
            this, &MainWindow::convertSelectionToSmartObject);

    m_smartObjectViaCopyAction =
        m_smartObjectMenu->addAction(tr("&New Smart Object via Copy"));
    ShortcutManager::instance()->registerAction("layer.smart_object.via_copy",
                                                m_smartObjectViaCopyAction);
    connect(m_smartObjectViaCopyAction, &QAction::triggered,
            this, &MainWindow::newSmartObjectViaCopy);

    m_smartObjectMenu->addSeparator();

    m_smartObjectEditAction = m_smartObjectMenu->addAction(tr("&Edit Contents"));
    ShortcutManager::instance()->registerAction("layer.smart_object.edit",
                                                m_smartObjectEditAction);
    connect(m_smartObjectEditAction, &QAction::triggered, this, [this]() {
        if (m_doc) editSmartObjectContents(m_doc->activeFlatIndex);
    });

    m_smartObjectReplaceAction = m_smartObjectMenu->addAction(tr("&Replace Contents..."));
    ShortcutManager::instance()->registerAction("layer.smart_object.replace",
                                                m_smartObjectReplaceAction);
    connect(m_smartObjectReplaceAction, &QAction::triggered,
            this, &MainWindow::replaceActiveSmartObjectContents);

    m_smartObjectRelinkAction = m_smartObjectMenu->addAction(tr("Relink to &File..."));
    ShortcutManager::instance()->registerAction("layer.smart_object.relink",
                                                m_smartObjectRelinkAction);
    connect(m_smartObjectRelinkAction, &QAction::triggered,
            this, &MainWindow::relinkActiveSmartObject);

    m_smartObjectUpdateAction = m_smartObjectMenu->addAction(tr("&Update Modified Content"));
    ShortcutManager::instance()->registerAction("layer.smart_object.update",
                                                m_smartObjectUpdateAction);
    connect(m_smartObjectUpdateAction, &QAction::triggered,
            this, &MainWindow::updateActiveSmartObjectContent);

    m_smartObjectMenu->addSeparator();

    m_smartObjectToLinkedAction = m_smartObjectMenu->addAction(tr("Convert to &Linked..."));
    ShortcutManager::instance()->registerAction("layer.smart_object.to_linked",
                                                m_smartObjectToLinkedAction);
    connect(m_smartObjectToLinkedAction, &QAction::triggered,
            this, [this]() { convertActiveSmartObjectStorage(/*toLinked=*/true); });

    m_smartObjectToEmbeddedAction = m_smartObjectMenu->addAction(tr("Embed Lin&ked"));
    ShortcutManager::instance()->registerAction("layer.smart_object.embed_linked",
                                                m_smartObjectToEmbeddedAction);
    connect(m_smartObjectToEmbeddedAction, &QAction::triggered,
            this, [this]() { convertActiveSmartObjectStorage(/*toLinked=*/false); });

    m_layerStylesAction = layerMenu->addAction(tr("Layer &Styles..."));
    ShortcutManager::instance()->registerAction("layer.styles", m_layerStylesAction);
    connect(m_layerStylesAction, &QAction::triggered, this, &MainWindow::onMenuLayerStyles);

    m_aiUpscaleLayerAction = layerMenu->addAction(tr("AI &Upscale..."));
    ShortcutManager::instance()->registerAction("layer.ai_upscale", m_aiUpscaleLayerAction);
    connect(m_aiUpscaleLayerAction, &QAction::triggered, this, &MainWindow::onAiUpscaleLayer);

    layerMenu->addSeparator();

    auto* maskMenu = layerMenu->addMenu(tr("Layer &Mask"));
    m_layerMaskAddAction = maskMenu->addAction(tr("&Add Mask"));
    ShortcutManager::instance()->registerAction("mask.add", m_layerMaskAddAction);
    connect(m_layerMaskAddAction, &QAction::triggered, this, &MainWindow::onMenuLayerMaskAdd);
    m_layerMaskRemoveAction = maskMenu->addAction(tr("&Remove Mask"));
    ShortcutManager::instance()->registerAction("mask.remove", m_layerMaskRemoveAction);
    connect(m_layerMaskRemoveAction, &QAction::triggered, this, &MainWindow::onMenuLayerMaskRemove);
    m_layerMaskApplyAction = maskMenu->addAction(tr("&Apply Mask"));
    ShortcutManager::instance()->registerAction("mask.apply", m_layerMaskApplyAction);
    connect(m_layerMaskApplyAction, &QAction::triggered, this, &MainWindow::onMenuLayerMaskApply);
    m_layerMaskToggleAction = maskMenu->addAction(tr("&Disable Mask"));
    ShortcutManager::instance()->registerAction("mask.toggle", m_layerMaskToggleAction);
    connect(m_layerMaskToggleAction, &QAction::triggered, this, &MainWindow::onMenuLayerMaskToggle);

    maskMenu->addSeparator();
    m_layerMaskFromSelAction = maskMenu->addAction(tr("From &Selection"));
    ShortcutManager::instance()->registerAction("mask.from_selection", m_layerMaskFromSelAction);
    connect(m_layerMaskFromSelAction, &QAction::triggered, this, [this]() {
        if (m_ctrl) m_ctrl->createMaskFromSelection(m_ctrl->activeLayerIndex());
    });
    m_layerMaskInvertAction = maskMenu->addAction(tr("&Invert Mask"));
    ShortcutManager::instance()->registerAction("mask.invert", m_layerMaskInvertAction);
    connect(m_layerMaskInvertAction, &QAction::triggered, this, [this]() {
        if (m_ctrl) m_ctrl->invertLayerMask(m_ctrl->activeLayerIndex());
    });
    maskMenu->addSeparator();
    m_layerMaskCopyAction = maskMenu->addAction(tr("&Copy Mask"));
    ShortcutManager::instance()->registerAction("mask.copy", m_layerMaskCopyAction);
    connect(m_layerMaskCopyAction, &QAction::triggered, this, [this]() {
        if (m_ctrl) m_ctrl->copyLayerMask(m_ctrl->activeLayerIndex());
    });
    m_layerMaskPasteAction = maskMenu->addAction(tr("&Paste Mask"));
    ShortcutManager::instance()->registerAction("mask.paste", m_layerMaskPasteAction);
    connect(m_layerMaskPasteAction, &QAction::triggered, this, [this]() {
        if (m_ctrl) m_ctrl->pasteLayerMask(m_ctrl->activeLayerIndex());
    });
    m_layerMaskClearAction = maskMenu->addAction(tr("C&lear Mask"));
    ShortcutManager::instance()->registerAction("mask.clear", m_layerMaskClearAction);
    connect(m_layerMaskClearAction, &QAction::triggered, this, [this]() {
        if (m_ctrl) m_ctrl->clearLayerMask(m_ctrl->activeLayerIndex());
    });

    layerMenu->addSeparator();

    m_flipHorizontalAction = layerMenu->addAction(tr("Flip &Horizontal"));
    ShortcutManager::instance()->registerAction("layer.flip_h", m_flipHorizontalAction);
    connect(m_flipHorizontalAction, &QAction::triggered, this, &MainWindow::onMenuFlipHorizontal);

    m_flipVerticalAction = layerMenu->addAction(tr("Flip &Vertical"));
    ShortcutManager::instance()->registerAction("layer.flip_v", m_flipVerticalAction);
    connect(m_flipVerticalAction, &QAction::triggered, this, &MainWindow::onMenuFlipVertical);

    m_resizeLayerAction = layerMenu->addAction(tr("&Resize Layer..."));
    ShortcutManager::instance()->registerAction("layer.resize", m_resizeLayerAction);
    connect(m_resizeLayerAction, &QAction::triggered, this, &MainWindow::onMenuResizeLayer);

    auto* imageMenu = mainMenuBar->addMenu(tr("I&mage"));

    auto* imageSizeAction = imageMenu->addAction(tr("Image &Size..."));
    ShortcutManager::instance()->registerAction("image.resize_image", imageSizeAction);
    connect(imageSizeAction, &QAction::triggered, this, &MainWindow::onImageResizeImage);

    auto* canvasSizeAction = imageMenu->addAction(tr("Canvas Si&ze..."));
    ShortcutManager::instance()->registerAction("image.resize_canvas", canvasSizeAction);
    connect(canvasSizeAction, &QAction::triggered, this, &MainWindow::onImageResizeCanvas);

    imageMenu->addSeparator();

    auto addSection = [](QMenu* menu, const QString& title) {
        auto* sep = menu->addSeparator();
        auto* section = menu->addAction(title);
        section->setEnabled(false);
        return sep;
    };

    addSection(imageMenu, tr("Adjust"));
    m_colorAdjAction = imageMenu->addAction(tr("&Color Adjustments..."));
    ShortcutManager::instance()->registerAction("image.adjust", m_colorAdjAction);
    connect(m_colorAdjAction, &QAction::triggered, this, &MainWindow::onImageColorAdjustments);
    auto* autoContrastAction = imageMenu->addAction(tr("&Auto Contrast"));
    ShortcutManager::instance()->registerAction("image.auto_contrast", autoContrastAction);
    connect(autoContrastAction, &QAction::triggered, this, &MainWindow::onImageAutoContrast);

    addSection(imageMenu, tr("Filter"));
    auto* blurMenu = imageMenu->addMenu(tr("&Blur"));
    m_gaussianAction = blurMenu->addAction(tr("&Gaussian..."));
    ShortcutManager::instance()->registerAction("image.gaussian_blur", m_gaussianAction);
    connect(m_gaussianAction, &QAction::triggered, this, &MainWindow::onImageGaussianBlur);
    m_boxBlurAction = blurMenu->addAction(tr("Bo&x..."));
    ShortcutManager::instance()->registerAction("image.box_blur", m_boxBlurAction);
    connect(m_boxBlurAction, &QAction::triggered, this, &MainWindow::onImageBoxBlur);
    m_medianAction = blurMenu->addAction(tr("&Median..."));
    ShortcutManager::instance()->registerAction("image.median_blur", m_medianAction);
    connect(m_medianAction, &QAction::triggered, this, &MainWindow::onImageMedianBlur);
    m_bilateralBlurAction = blurMenu->addAction(tr("&Bilateral..."));
    ShortcutManager::instance()->registerAction("image.bilateral_blur", m_bilateralBlurAction);
    connect(m_bilateralBlurAction, &QAction::triggered, this, &MainWindow::onImageBilateralBlur);
    m_motionBlurAction = blurMenu->addAction(tr("Mo&tion..."));
    ShortcutManager::instance()->registerAction("image.motion_blur", m_motionBlurAction);
    connect(m_motionBlurAction, &QAction::triggered, this, &MainWindow::onImageMotionBlur);
    m_radialBlurAction = blurMenu->addAction(tr("&Radial..."));
    ShortcutManager::instance()->registerAction("image.radial_blur", m_radialBlurAction);
    connect(m_radialBlurAction, &QAction::triggered, this, &MainWindow::onImageRadialBlur);
    m_zoomBlurAction = blurMenu->addAction(tr("&Zoom..."));
    ShortcutManager::instance()->registerAction("image.zoom_blur", m_zoomBlurAction);
    connect(m_zoomBlurAction, &QAction::triggered, this, &MainWindow::onImageZoomBlur);

    m_sharpenAction = imageMenu->addAction(tr("&Sharpen..."));
    ShortcutManager::instance()->registerAction("image.sharpen", m_sharpenAction);
    connect(m_sharpenAction, &QAction::triggered, this, &MainWindow::onImageSharpen);

    addSection(imageMenu, tr("AI"));
    m_aiUpscaleDocumentAction = imageMenu->addAction(tr("&Upscale..."));
    ShortcutManager::instance()->registerAction("image.ai_upscale", m_aiUpscaleDocumentAction);
    connect(m_aiUpscaleDocumentAction, &QAction::triggered, this, &MainWindow::onAiUpscaleDocument);

    auto* generativeFillAction = imageMenu->addAction(tr("&Generative Fill..."));
    connect(generativeFillAction, &QAction::triggered, this, &MainWindow::onGenerativeFill);

    auto* stylizeMenu = imageMenu->addMenu(tr("S&tylize"));
    m_posterizeAction = stylizeMenu->addAction(tr("&Posterize..."));
    ShortcutManager::instance()->registerAction("image.posterize", m_posterizeAction);
    connect(m_posterizeAction, &QAction::triggered, this, &MainWindow::onImagePosterize);
    m_thresholdAction = stylizeMenu->addAction(tr("&Threshold..."));
    ShortcutManager::instance()->registerAction("image.threshold", m_thresholdAction);
    connect(m_thresholdAction, &QAction::triggered, this, &MainWindow::onImageThreshold);

    auto* detectMenu = imageMenu->addMenu(tr("&Detection"));
    m_edgesAction = detectMenu->addAction(tr("&Edges..."));
    ShortcutManager::instance()->registerAction("image.edges", m_edgesAction);
    connect(m_edgesAction, &QAction::triggered, this, &MainWindow::onImageEdgeDetect);

    auto* utilityMenu = imageMenu->addMenu(tr("&Utility"));
    m_grayscaleAction = utilityMenu->addAction(tr("&Grayscale"));
    ShortcutManager::instance()->registerAction("image.grayscale", m_grayscaleAction);
    connect(m_grayscaleAction, &QAction::triggered, this, &MainWindow::onImageGrayscale);
    m_invertAction = utilityMenu->addAction(tr("&Invert"));
    ShortcutManager::instance()->registerAction("image.invert", m_invertAction);
    connect(m_invertAction, &QAction::triggered, this, &MainWindow::onImageInvert);
    m_noiseAction = utilityMenu->addAction(tr("&Noise Reduce..."));
    ShortcutManager::instance()->registerAction("image.noise_reduce", m_noiseAction);
    connect(m_noiseAction, &QAction::triggered, this, &MainWindow::onImageNoiseReduce);
    m_removeBgAction = utilityMenu->addAction(tr("&Remove Background..."));
    ShortcutManager::instance()->registerAction("image.remove_bg", m_removeBgAction);
    connect(m_removeBgAction, &QAction::triggered, this, &MainWindow::onImageRemoveBg);

    // Select: the selection ↔ channel round trip. New menu rather than extra
    // Edit entries — these commands belong with the selection, and the panel
    // buttons open the very same dialogs.
    auto* selectMenu = mainMenuBar->addMenu(tr("&Select"));
    auto* saveSelectionAction = selectMenu->addAction(tr("&Save Selection..."));
    connect(saveSelectionAction, &QAction::triggered,
            this, &MainWindow::showSaveSelectionDialog);
    auto* loadSelectionAction = selectMenu->addAction(tr("&Load Selection..."));
    connect(loadSelectionAction, &QAction::triggered,
            this, &MainWindow::showLoadSelectionDialog);
    selectMenu->addSeparator();
    // No shortcut registered here on purpose: Q already reaches
    // CanvasView::toggleQuickMask through the ShortcutManager dispatcher
    // (canvas.quick_mask), and binding it to this QAction too would toggle the
    // mode twice per press.
    auto* quickMaskAction = selectMenu->addAction(tr("Edit in &Quick Mask Mode"));
    connect(quickMaskAction, &QAction::triggered, this, [this]() {
        if (m_canvas) m_canvas->toggleQuickMask();
    });

    auto* viewMenu = mainMenuBar->addMenu(tr("&View"));
    auto* zoomInAction = viewMenu->addAction(tr("Zoom &In"));
    ShortcutManager::instance()->registerAction("view.zoom_in", zoomInAction);
    connect(zoomInAction, &QAction::triggered, this, [this]() {
        if (m_canvas) m_canvas->zoomIn();
    });

    auto* zoomOutAction = viewMenu->addAction(tr("Zoom &Out"));
    ShortcutManager::instance()->registerAction("view.zoom_out", zoomOutAction);
    connect(zoomOutAction, &QAction::triggered, this, [this]() {
        if (m_canvas) m_canvas->zoomOut();
    });

    auto* zoomFitAction = viewMenu->addAction(tr("&Fit to Screen"));
    ShortcutManager::instance()->registerAction("view.zoom_fit", zoomFitAction);
    connect(zoomFitAction, &QAction::triggered, this, [this]() {
        if (m_canvas) m_canvas->fitToView();
    });

    auto* actualSizeAction = viewMenu->addAction(tr("&Actual Size"));
    ShortcutManager::instance()->registerAction("view.actual_size", actualSizeAction);
    connect(actualSizeAction, &QAction::triggered, this, [this]() {
        if (m_canvas) m_canvas->zoomToOriginal();
    });

    auto* resetViewAction = viewMenu->addAction(tr("&Reset View"));
    ShortcutManager::instance()->registerAction("view.reset", resetViewAction);
    connect(resetViewAction, &QAction::triggered, this, [this]() {
        if (m_canvas) m_canvas->zoomToOriginal();
    });

    viewMenu->addSeparator();

    m_overscrollAction = viewMenu->addAction(tr("&Overscroll"));
    m_overscrollAction->setCheckable(true);
    m_overscrollAction->setChecked(core::ViewportCamera::overscrollEnabled());
    m_overscrollAction->setToolTip(
        tr("Allow dragging the canvas past the viewport edges"));
    connect(m_overscrollAction, &QAction::toggled, this, [this](bool checked) {
        core::ViewportCamera::setOverscrollEnabled(checked);
        for (auto& tab : m_tabs) {
            if (tab.canvas) {
                tab.canvas->setOverscrollEnabled(checked);
                tab.canvas->update();
            }
        }
    });

    viewMenu->addSeparator();

    // ── Soft proof (display-only colour preview) ─────────────────────────
    m_proofColorsAction = viewMenu->addAction(tr("&Proof Colors"));
    m_proofColorsAction->setCheckable(true);
    // Ctrl+Y is the conventional Proof Colors shortcut; it is unused elsewhere in the app. Set directly
    // (not via ShortcutManager) because the proof actions are not part of its
    // fixed action registry and registering an unknown id would clear the key.
    m_proofColorsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y));
    connect(m_proofColorsAction, &QAction::toggled, this, [this](bool checked) {
        if (!m_doc) return;
        SoftProofSettings proof = m_doc->softProofSettings();
        proof.enabled = checked;
        // First enable with no proof target → default to sRGB so the toggle has
        // a visible, predictable effect instead of silently doing nothing.
        if (checked && !proof.proofProfile.isValid())
            proof.proofProfile = ColorProfile::sRgb();
        m_doc->setSoftProofSettings(proof);
        updateStatusFromDocument();
        if (m_canvas)
            m_canvas->update();
    });

    auto* proofSetupAction = viewMenu->addAction(tr("Proof &Setup..."));
    connect(proofSetupAction, &QAction::triggered, this, [this]() {
        if (!m_doc) return;
        SoftProofDialog dlg(m_doc->softProofSettings(), this);
        if (dlg.exec() != QDialog::Accepted)
            return;
        SoftProofSettings proof = dlg.settings();
        m_doc->setSoftProofSettings(proof);
        if (m_proofColorsAction) {
            QSignalBlocker block(m_proofColorsAction);
            m_proofColorsAction->setChecked(proof.enabled);
        }
        updateStatusFromDocument();
        if (m_canvas)
            m_canvas->update();
    });

    viewMenu->addSeparator();

    auto applyOverlaySettings = [this](const std::function<void(RulerGuideSettings&)>& mutate) {
        applyRulerGuideSetting(mutate);
    };

    m_showColorPaletteAction = viewMenu->addAction(tr("Color &Palette"));
    m_showColorPaletteAction->setCheckable(true);
    {
        QSettings s;
        const bool visible = s.value(QStringLiteral("View/colorPaletteVisible"), false).toBool();
        m_showColorPaletteAction->setChecked(visible);
    }
    connect(m_showColorPaletteAction, &QAction::toggled, this, [this](bool checked) {
        QSettings s;
        s.setValue(QStringLiteral("View/colorPaletteVisible"), checked);
        if (m_colorPaletteBar)
            m_colorPaletteBar->setVisible(checked);
    });

    m_showRulersAction = viewMenu->addAction(tr("Show &Rulers"));
    m_showRulersAction->setCheckable(true);
    ShortcutManager::instance()->registerAction("view.show_rulers", m_showRulersAction);
    connect(m_showRulersAction, &QAction::toggled, this, [applyOverlaySettings](bool checked) {
        applyOverlaySettings([checked](RulerGuideSettings& s) { s.rulers.showRulers = checked; });
    });

    m_showGuidesAction = viewMenu->addAction(tr("Show &Guides"));
    m_showGuidesAction->setCheckable(true);
    ShortcutManager::instance()->registerAction("view.show_guides", m_showGuidesAction);
    connect(m_showGuidesAction, &QAction::toggled, this, [applyOverlaySettings](bool checked) {
        applyOverlaySettings([checked](RulerGuideSettings& s) { s.guides.showGuides = checked; });
    });

    m_lockGuidesAction = viewMenu->addAction(tr("&Lock Guides"));
    m_lockGuidesAction->setCheckable(true);
    ShortcutManager::instance()->registerAction("view.lock_guides", m_lockGuidesAction);
    connect(m_lockGuidesAction, &QAction::toggled, this, [applyOverlaySettings](bool checked) {
        applyOverlaySettings([checked](RulerGuideSettings& s) { s.guides.lockGuides = checked; });
    });

    auto* clearGuidesAction = viewMenu->addAction(tr("Clear Guides"));
    ShortcutManager::instance()->registerAction("view.clear_guides", clearGuidesAction);
    connect(clearGuidesAction, &QAction::triggered, this, [this]() {
        if (auto* ctrl = activeController())
            ctrl->clearGuides();
        if (auto* canvas = activeCanvas())
            canvas->update();
    });

    auto* snapMenu = viewMenu->addMenu(tr("&Snap"));
    m_enableSnapAction = snapMenu->addAction(tr("Enable Snap"));
    m_enableSnapAction->setCheckable(true);
    ShortcutManager::instance()->registerAction("view.enable_snap", m_enableSnapAction);
    connect(m_enableSnapAction, &QAction::toggled, this, [applyOverlaySettings](bool checked) {
        applyOverlaySettings([checked](RulerGuideSettings& s) { s.snap.enabled = checked; });
    });

    m_snapToCanvasAction = snapMenu->addAction(tr("Snap to Canvas"));
    m_snapToCanvasAction->setCheckable(true);
    ShortcutManager::instance()->registerAction("view.snap_to_canvas", m_snapToCanvasAction);
    connect(m_snapToCanvasAction, &QAction::toggled, this, [applyOverlaySettings](bool checked) {
        applyOverlaySettings([checked](RulerGuideSettings& s) {
            s.snap.snapToCanvasBounds = checked;
            s.snap.snapToCanvasCenter = checked;
        });
    });

    m_snapToGuidesAction = snapMenu->addAction(tr("Snap to Guides"));
    m_snapToGuidesAction->setCheckable(true);
    ShortcutManager::instance()->registerAction("view.snap_to_guides", m_snapToGuidesAction);
    connect(m_snapToGuidesAction, &QAction::toggled, this, [applyOverlaySettings](bool checked) {
        applyOverlaySettings([checked](RulerGuideSettings& s) {
            s.snap.snapToGuides = checked;
            s.guides.snapToGuides = checked;
        });
    });

    snapMenu->addSeparator();
    auto* snapSettingsAction = snapMenu->addAction(tr("Snap Settings..."));
    ShortcutManager::instance()->registerAction("view.snap_settings", snapSettingsAction);
    connect(snapSettingsAction, &QAction::triggered, this, [this]() {
        showAppSettings(AppSettingsDialog::PageInterface, [this](int result) {
            if (result != QDialog::Accepted)
                return;
            reloadRulerGuideSettingsForTabs();
            updateRulerGuideActionStates();
        });
    });
    updateRulerGuideActionStates();

    auto* windowMenu = mainMenuBar->addMenu(tr("&Window"));
    auto* colorPickerAction = windowMenu->addAction(tr("&Color Picker..."));
    ShortcutManager::instance()->registerAction("window.color_picker", colorPickerAction);
    connect(colorPickerAction, &QAction::triggered, this, &MainWindow::onMenuColorPicker);

    auto* brushesAction = windowMenu->addAction(tr("&Brushes"));
    connect(brushesAction, &QAction::triggered, this, &MainWindow::showBrushPanel);

    auto* customShapesAction = windowMenu->addAction(tr("Custom &Shapes"));
    connect(customShapesAction, &QAction::triggered, this, &MainWindow::showCustomShapesPanel);

    auto* brushSettingsAction = windowMenu->addAction(tr("Brush &Settings"));
    connect(brushSettingsAction, &QAction::triggered, this, &MainWindow::showBrushSettingsPanel);

    auto* windowGenFillAction = windowMenu->addAction(tr("&Generative Fill"));
    connect(windowGenFillAction, &QAction::triggered, this, &MainWindow::onGenerativeFill);

    // Rebuilt every time it opens (see rebuildWorkspacesMenu): the preset list
    // and which commands apply both depend on state the menu cannot cache.
    m_workspacesMenu = windowMenu->addMenu(tr("&Workspaces"));
    connect(m_workspacesMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildWorkspacesMenu);

    auto* helpMenu = mainMenuBar->addMenu(tr("&Help"));
    auto* docsAction = helpMenu->addAction(tr("Documentation"));
    connect(docsAction, &QAction::triggered, this, []() {
        QDesktopServices::openUrl(QUrl(QStringLiteral("http://rincolabs.org/projects/hazor-studio")));
    });
    helpMenu->addSeparator();
    auto* diagnosticsMenu = helpMenu->addMenu(tr("&Diagnostics"));
    auto* openAiLogsAction = diagnosticsMenu->addAction(tr("Open AI Logs Folder"));
    connect(openAiLogsAction, &QAction::triggered, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::subDir(QStringLiteral("logs/ai"))));
    });
    helpMenu->addSeparator();
    auto* aboutAction = helpMenu->addAction(tr("About %1").arg(QApplication::applicationName()));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onAboutDialog);

    // Place the fully-built menu bar: inside the custom title bar when available,
    // otherwise in the traditional QMainWindow menu position as a fallback.
    if (m_useCustomTitleBar && m_titleBar) {
        m_titleBar->setMenuBar(mainMenuBar);
    } else {
        setMenuBar(mainMenuBar);
    }

    m_toolBar = new ToolOptionsBar(tr("Tool Options"), this);
    m_toolBar->setMovable(false);
    m_toolBar->setFloatable(false);
    m_toolBar->setFixedHeight(45);
    addToolBar(Qt::TopToolBarArea, m_toolBar);

    // Align bar removed from the options bar (it now lives in the Properties
    // panel, which drives alignment via its own AlignBar). Not created here:
    // parenting it to the toolbar without adding it to a layout makes it float.
    // m_alignBar stays nullptr; updateAlignBarState() guards against that.
    // m_alignBar = new AlignBar(tr("Align"), m_toolBar);
    // m_toolBar->setAuxiliaryOptionsWidget(m_alignBar);
    // m_toolBar->setAuxiliaryOptionsVisible(false);
    updateToolBarLayout();
    // The Align page is hidden until a layout-bearing tool + compatible layer
    // make it relevant (no document/tool yet at construction → hidden).
    updateAlignBarState();

    auto* centralHost = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(centralHost);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    auto setupTabBg = [](QTabWidget* tw, const QColor& bg) {
        if (!tw) return;
        tw->setAutoFillBackground(true);
        QPalette p;
        p.setColor(QPalette::Window, bg);
        tw->setPalette(p);
        auto* tb = tw->tabBar();
        tb->setAutoFillBackground(true);
        tb->setPalette(p);
    };
    auto* thInit = ThemeManager::instance()->current();

    // A floatable panel reparents into a private dock group window when it is
    // tab-grouped (or floated), which drops any background it borrowed from the
    // QDockWidget frame — the global QWidget stylesheet defines only the font,
    // no background, so the panel falls back to the default window colour and
    // "loses the theme". Give each floatable panel a themed background scoped to
    // its own class (WA_StyledBackground so a plain QWidget subclass paints it),
    // appended so any stylesheet the panel set on itself is preserved. Used for
    // every floatable dock content below — no per-panel special-casing.
    auto themePanelBg = [](QWidget* panel, const Theme* t) {
        if (!panel || !t) return;
        panel->setAttribute(Qt::WA_StyledBackground, true);
        panel->setStyleSheet(panel->styleSheet() +
            QStringLiteral("\n%1 { background: %2; }")
                .arg(QString::fromLatin1(panel->metaObject()->className()),
                     t->colorSurface.name()));
    };

    m_tabWidget = new QTabWidget(centralHost);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    m_tabWidget->tabBar()->setElideMode(Qt::ElideRight);
    m_homeWorkspace = new HomeWorkspace(m_tabWidget);
    const int homeIndex = m_tabWidget->addTab(m_homeWorkspace, tr("Home"));
    m_tabWidget->setTabToolTip(homeIndex, tr("Home"));
    // QTabWidget owns one close button policy for all pages; clear the Home
    // button after adding it so the fixed welcome page can never be closed.
    m_tabWidget->tabBar()->setTabButton(homeIndex, QTabBar::LeftSide, nullptr);
    m_tabWidget->tabBar()->setTabButton(homeIndex, QTabBar::RightSide, nullptr);
    setupTabBg(m_tabWidget, thInit->colorPanelBackground);
    centralLayout->addWidget(m_tabWidget, 1);

    m_viewportStatusBar = new QWidget(centralHost);
    m_viewportStatusBar->setObjectName(QStringLiteral("ViewportStatusBar"));
    m_viewportStatusBar->setFixedHeight(32);

    auto* statusLayout = new QHBoxLayout(m_viewportStatusBar);
    statusLayout->setContentsMargins(
        thInit->spaceMD, thInit->spaceXS,
        thInit->spaceMD, thInit->spaceXS);
    statusLayout->setSpacing(thInit->spaceLG);

    // Icon shown immediately before status messages for blocked/locked ops;
    // hidden for normal "Ready"/info messages. Placeholder art lives at
    // :/icons/status-lock.png — swap the PNG to change it.
    m_statusIconLabel = new QLabel(m_viewportStatusBar);
    m_statusIconLabel->setObjectName(QStringLiteral("ViewportStatusIcon"));
    m_statusIconLabel->setFixedSize(16, 16);
    m_statusIconLabel->setScaledContents(true);
    m_statusIconLabel->setVisible(false);

    m_statusMessageLabel = new QLabel(tr("Ready"), m_viewportStatusBar);
    m_statusMessageLabel->setObjectName(QStringLiteral("ViewportStatusMessage"));
    m_statusMessageLabel->setMinimumWidth(0);
    m_statusMessageLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    m_zoomCombo = new ZoomComboBox(m_viewportStatusBar);
    m_zoomCombo->setToolTip(tr("Zoom"));

    m_coordLabel = new QLabel(tr("X: —  Y: —"), m_viewportStatusBar);
    m_sizeLabel = new QLabel(tr("Size: —"), m_viewportStatusBar);
    m_colorProfileLabel = new QLabel(tr("Color: —"), m_viewportStatusBar);

    statusLayout->addWidget(m_statusIconLabel, 0, Qt::AlignVCenter);
    statusLayout->addWidget(m_statusMessageLabel, 1);
    statusLayout->addWidget(m_coordLabel, 0, Qt::AlignVCenter);
    statusLayout->addWidget(m_sizeLabel, 0, Qt::AlignVCenter);
    statusLayout->addWidget(m_colorProfileLabel, 0, Qt::AlignVCenter);

    m_statusZoomFitBtn = new QPushButton(m_viewportStatusBar);
    m_statusZoomFitBtn->setIcon(makeIcon(":/icons/zoom-fit.png"));
    m_statusZoomFitBtn->setIconSize(QSize(24, 24));
    m_statusZoomFitBtn->setFixedSize(26, 26);
    m_statusZoomFitBtn->setToolTip(tr("Fit canvas to viewport"));
    statusLayout->addWidget(m_statusZoomFitBtn, 0, Qt::AlignVCenter);

    m_statusZoom100Btn = new QPushButton(m_viewportStatusBar);
    m_statusZoom100Btn->setIcon(makeIcon(":/icons/zoom-1-1.png"));
    m_statusZoom100Btn->setIconSize(QSize(24, 24));
    m_statusZoom100Btn->setFixedSize(26, 26);
    m_statusZoom100Btn->setToolTip(tr("Zoom to original size (100%)"));
    statusLayout->addWidget(m_statusZoom100Btn, 0, Qt::AlignVCenter);

    centralLayout->addWidget(m_viewportStatusBar, 0);
    statusLayout->addWidget(m_zoomCombo, 0, Qt::AlignVCenter);

    m_colorPaletteBar = new ColorPaletteBar(centralHost);
    centralLayout->addWidget(m_colorPaletteBar, 0);
    if (m_showColorPaletteAction)
        m_colorPaletteBar->setVisible(m_showColorPaletteAction->isChecked());
    setCentralWidget(centralHost);
    applyViewportStatusBarTheme();

    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, &MainWindow::onTabCloseRequested);
    connect(m_tabWidget, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(m_tabWidget->tabBar(), &QTabBar::tabMoved, this, &MainWindow::onTabMoved);
    connect(m_homeWorkspace, &HomeWorkspace::newDocumentRequested,
            this, &MainWindow::onNewFile);
    connect(m_homeWorkspace, &HomeWorkspace::openDocumentRequested,
            this, &MainWindow::onOpenFile);
    connect(m_homeWorkspace, &HomeWorkspace::recentDocumentRequested,
            this, &MainWindow::onOpenRecentFile);
    connect(m_homeWorkspace, &HomeWorkspace::templateRequested,
            this, &MainWindow::onHomeTemplateRequested);

    // Minimal, stable docking config. GroupedDragging keeps the ability to drag
    // a floating panel onto another to group them as tabs. We deliberately do
    // NOT set AllowNestedDocks (so the user can't create side-by-side splits —
    // dropping onto a dock tabs it) nor ForceTabbedDocks. ForceTabbedDocks was
    // forcing a tab layout even on a 1-dock floating group, which crashed Qt's
    // layout activation during the 2→1 tab transition (drag a tab out of a
    // floating group). setTabPosition(AllDockWidgetAreas, …) is also gone — it
    // was the source of the "tabPosition out-of-bounds '0'" warnings (floating
    // groups have dock area 0). Programmatic splitDockWidget (our two-group
    // default) still works regardless.
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks |
                   QMainWindow::GroupedDragging);
    // Tabs of tabified docks at the top (docked areas; floating groups keep Qt's
    // default since their dock area is 0).
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

    auto* toolsDock = new QDockWidget(tr("Tools"), this);
    toolsDock->setObjectName(QStringLiteral("ToolsDock"));
    toolsDock->setAllowedAreas(Qt::LeftDockWidgetArea);
    toolsDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    toolsDock->setTitleBarWidget(new QWidget(toolsDock));
    m_toolsPanel = new ToolsPanel(m_colorEngine, this);
    toolsDock->setWidget(m_toolsPanel);
    addDockWidget(Qt::LeftDockWidgetArea, toolsDock);

    m_historyPanel = new HistoryPanel(this);
    m_channelsPanel = new ChannelsPanel(this);
    m_agentPanel = new AgentPanel(m_agentController, this);
    m_propsPanel = new PropertiesPanel(this);
    m_histogramPanel = new HistogramPanel(this);
    m_swatchesPanel = new SwatchesPanel(m_colorEngine, this);
    m_colorMixerPanel = new ColorMixerPanel(m_colorEngine, this);

    m_layerPanel = new LayerPanel(this);

    

    // Every right-side panel is its own dockable QDockWidget so the user can drag
    // an individual tab out and regroup it across docks — native QTabWidget tabs
    // cannot be moved between docks. They are tabified below to reproduce the
    // default two-group layout, but the grouping is now fully user-rearrangeable.
    windowMenu->addSeparator();
    auto makePanelDock = [&](const QString& title, const QString& objName,
                             QWidget* content) -> QDockWidget* {
        auto* dock = new QDockWidget(title, this);
        dock->setObjectName(objName);
        dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
        dock->setFeatures(QDockWidget::DockWidgetMovable |
                          QDockWidget::DockWidgetFloatable |
                          QDockWidget::DockWidgetClosable);
        dock->setWidget(content);
        watchDockTitleBar(dock);

        themePanelBg(content, thInit);
        windowMenu->addAction(dock->toggleViewAction()); // reopen after close

        return dock;
    };

    m_layersDock    = makePanelDock(tr("Layers"),     QStringLiteral("LayersDock"),     m_layerPanel);
    m_agentDock     = makePanelDock(tr("AI Agent"),   QStringLiteral("AgentDock"),      m_agentPanel);
    m_histogramDock = makePanelDock(tr("Histogram"),  QStringLiteral("HistogramDock"),  m_histogramPanel);
    m_propsDock     = makePanelDock(tr("Properties"), QStringLiteral("PropertiesDock"), m_propsPanel);
    m_colorDock     = makePanelDock(tr("Color"),      QStringLiteral("ColorDock"),      m_colorMixerPanel);
    m_swatchesDock  = makePanelDock(tr("Swatches"),   QStringLiteral("SwatchesDock"),   m_swatchesPanel);
    m_channelsDock  = makePanelDock(tr("Channels"),   QStringLiteral("ChannelsDock"),   m_channelsPanel);

    connect(m_channelsPanel, &ChannelsPanel::splitChannelsRequested,
            this, &MainWindow::splitChannelsIntoDocuments);
    connect(m_channelsPanel, &ChannelsPanel::mergeChannelsRequested,
            this, &MainWindow::mergeChannelsFromDocuments);
    connect(m_channelsPanel, &ChannelsPanel::duplicateChannelRequested,
            this, &MainWindow::duplicateChannelWithDialog);
    connect(m_channelsPanel, &ChannelsPanel::saveSelectionDialogRequested,
            this, &MainWindow::showSaveSelectionDialog);
    connect(m_channelsPanel, &ChannelsPanel::loadSelectionDialogRequested,
            this, &MainWindow::showLoadSelectionDialog);
    m_historyDock   = makePanelDock(tr("History"),    QStringLiteral("HistoryDock"),    m_historyPanel);

    connect(m_propsPanel, &PropertiesPanel::maskDensityChanged, this, [this](float density) {
        if (m_ctrl) m_ctrl->setMaskDensity(m_ctrl->activeLayerIndex(), density);
    });
    connect(m_propsPanel, &PropertiesPanel::maskFeatherBegin, this, [this]() {
        if (m_ctrl) m_ctrl->beginMaskFeather(m_ctrl->activeLayerIndex());
    });
    connect(m_propsPanel, &PropertiesPanel::maskFeatherPreview, this, [this](float radius) {
        if (m_ctrl) m_ctrl->previewMaskFeather(m_ctrl->activeLayerIndex(), radius);
    });
    connect(m_propsPanel, &PropertiesPanel::maskFeatherCommit, this, [this](float radius) {
        if (m_ctrl) m_ctrl->commitMaskFeather(m_ctrl->activeLayerIndex(), radius);
    });
    connect(m_propsPanel, &PropertiesPanel::maskInvertRequested, this, [this]() {
        if (m_ctrl) m_ctrl->invertLayerMask(m_ctrl->activeLayerIndex());
    });
    connect(m_propsPanel, &PropertiesPanel::maskOverlayToggled, this, [this](bool visible) {
        if (m_ctrl) m_ctrl->setMaskOverlayVisible(visible);
    });
    connect(m_propsPanel, &PropertiesPanel::maskOverlayOpacityChanged, this, [this](float opacity) {
        if (m_ctrl) m_ctrl->setMaskOverlayOpacity(opacity);
    });

    // ── Transform / Align / Quick Actions: all shortcuts to existing editor logic. ──
    connect(m_propsPanel, &PropertiesPanel::transformFieldEdited, this,
            &MainWindow::applyPropertiesTransformEdit);
    connect(m_propsPanel, &PropertiesPanel::flipHorizontalRequested,
            this, &MainWindow::onMenuFlipHorizontal);
    connect(m_propsPanel, &PropertiesPanel::flipVerticalRequested,
            this, &MainWindow::onMenuFlipVertical);
    connect(m_propsPanel, &PropertiesPanel::alignRequested, this, [this](int type) {
        if (!m_canvas) return;
        switch (type) {
        case 0: m_canvas->alignLeft();    break;
        case 1: m_canvas->alignCenterH(); break;
        case 2: m_canvas->alignRight();   break;
        case 3: m_canvas->alignTop();     break;
        case 4: m_canvas->alignMiddleV(); break;
        case 5: m_canvas->alignBottom();  break;
        }
    });
    connect(m_propsPanel, &PropertiesPanel::alignTargetChanged, this, [this](int target) {
        if (m_canvas) m_canvas->setAlignTarget(target);
    });
    connect(m_propsPanel, &PropertiesPanel::resetTransformRequested, this, [this]() {
        if (!m_doc || !m_ctrl || m_doc->activeFlatIndex < 0) return;
        auto* node = m_doc->activeNode();
        if (!node || !node->layer || node->isPositionLocked()) return;
        if (!node->layer->hasResetTransform) {
            node->layer->resetTransform = node->transform;
            node->layer->hasResetTransform = true;
            return;
        }
        const QTransform before = node->transform;
        const QTransform after = node->layer->resetTransform;
        if (before == after) return;
        m_ctrl->setLayerTransform(m_doc->activeFlatIndex, after, &before);
    });
    connect(m_propsPanel, &PropertiesPanel::removeBackgroundRequested, this, [this]() {
        if (m_canvas && m_canvas->aiSelectionController())
            m_canvas->aiSelectionController()->removeBackground();
    });
    connect(m_propsPanel, &PropertiesPanel::selectSubjectRequested, this, [this]() {
        if (m_canvas && m_canvas->aiSelectionController())
            m_canvas->aiSelectionController()->selectSubject();
    });
    connect(m_propsPanel, &PropertiesPanel::aiUpscaleRequested, this, [this]() {
        showAiUpscaleDialog(UpscaleTarget::CurrentLayer);
    });

    // ── Smart Object section ──
    connect(m_propsPanel, &PropertiesPanel::smartObjectEditRequested, this, [this]() {
        if (m_doc) editSmartObjectContents(m_doc->activeFlatIndex);
    });
    connect(m_propsPanel, &PropertiesPanel::smartObjectUpdateRequested,
            this, &MainWindow::updateActiveSmartObjectContent);
    connect(m_propsPanel, &PropertiesPanel::smartObjectRelinkRequested,
            this, &MainWindow::relinkActiveSmartObject);

    // ── Document / Canvas page: all shortcuts to existing document logic. ──
    connect(m_propsPanel, &PropertiesPanel::canvasSizeEdited,
            this, &MainWindow::applyCanvasSizeEdit);
    connect(m_propsPanel, &PropertiesPanel::canvasPortraitRequested,
            this, [this]() { applyCanvasOrientation(true); });
    connect(m_propsPanel, &PropertiesPanel::canvasLandscapeRequested,
            this, [this]() { applyCanvasOrientation(false); });
    connect(m_propsPanel, &PropertiesPanel::rulersToggled, this, [this](bool visible) {
        applyRulerGuideSetting([visible](RulerGuideSettings& s) {
            s.rulers.showRulers = visible;
        });
    });
    connect(m_propsPanel, &PropertiesPanel::rulerUnitChanged, this, [this](int unit) {
        applyRulerGuideSetting([unit](RulerGuideSettings& s) {
            s.rulers.unit = static_cast<RulerUnit>(unit);
        });
    });
    // Curves editor eyedropper/target: arm the active canvas to sample a pixel.
    if (auto* curvesEditor = m_propsPanel->curvesEditor()) {
        connect(curvesEditor, &CurvesEditorWidget::requestImagePick, this,
                [this](int mode) {
            if (m_canvas) m_canvas->setCurvesPickMode(mode);
        });
    }
    // Hue/Saturation editor eyedroppers: arm the active canvas to sample the
    // adjustment's input colour (mode 1 = main, 2 = add, 3 = subtract).
    if (auto* hsEditor = m_propsPanel->hueSaturationEditor()) {
        connect(hsEditor, &HueSaturationAdjustmentWidget::requestImagePick, this,
                [this](int mode) {
            if (m_canvas) m_canvas->setHueSatPickMode(mode);
        });
    }

    // Minimum width: 12.5% of screen, applied to each group's anchor dock. The
    // actual default arrangement is applied once via resetDockLayout() at the end
    // of construction (shared with Window → Workspaces → Reset Workspace).
    int minW = QGuiApplication::primaryScreen()->availableGeometry().width() / 8;
    m_histogramDock->setMinimumWidth(minW);
    m_layersDock->setMinimumWidth(minW);

    auto* brushDock = new QDockWidget(tr("Brush Settings"), this);
    m_brushSettingsDock = brushDock;
    brushDock->setObjectName("BrushSettingsDock");
    brushDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    brushDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    m_brushDynamics = new BrushDynamicsPanel(brushDock);
    brushDock->setWidget(m_brushDynamics);
    themePanelBg(m_brushDynamics, thInit);
    if (m_toolBar)
        m_brushDynamics->setPresetManager(m_toolBar->presetManager());
    // Register with the main window's dock layout (not just parented to it),
    // otherwise it stays an independent floating window that can never tab with
    // other docks. setFloating(true) then pops it out while keeping it managed.
    addDockWidget(Qt::RightDockWidgetArea, brushDock);
    brushDock->setFloating(true);
    brushDock->resize(360, 560);
    brushDock->hide();

    // Brush browser panel (preset browser): a standalone
    // dockable panel, floating by default. It is the single official place for
    // brush navigation/selection — opened from the Brush Picker (options bar)
    // and Window → Brushes. Supports docking right + tabification like the
    // other panels (see feature-brush-picker).
    m_brushPanelDock = new QDockWidget(tr("Brushes"), this);
    m_brushPanelDock->setObjectName(QStringLiteral("BrushesDock"));
    m_brushPanelDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    m_brushPanelDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    m_brushPanel = new BrushPanel(this);
    m_brushPanelDock->setWidget(m_brushPanel);
    themePanelBg(m_brushPanel, thInit);
    if (m_toolBar)
        m_brushPanel->setPresetManager(m_toolBar->presetManager());
    addDockWidget(Qt::RightDockWidgetArea, m_brushPanelDock);
    m_brushPanelDock->setFloating(true);
    m_brushPanelDock->resize(360, 620);
    m_brushPanelDock->hide();

    connect(m_brushPanel, &BrushPanel::presetSelected, this,
            [this](const BrushPreset& preset) { applyBrushPreset(preset); });
    connect(m_brushPanel, &BrushPanel::brushSizeChanged, this, [this](float s) {
        if (m_canvas) m_canvas->setBrushSize(s);
        if (m_toolBar) m_toolBar->setBrushSize(qRound(s));
    });
    connect(m_brushPanel, &BrushPanel::brushHardnessChanged, this, [this](float h) {
        if (m_canvas) m_canvas->setBrushHardness(h);
        if (m_toolBar) m_toolBar->setBrushHardness(qRound(h * 100.0f));
    });
    connect(m_brushPanel, &BrushPanel::importBrushesRequested, this,
            [this]() { if (m_importBrushesAction) m_importBrushesAction->trigger(); });
    connect(m_brushPanel, &BrushPanel::openInSettingsRequested,
            this, &MainWindow::showBrushSettingsPanel);
    connect(m_brushPanel, &BrushPanel::brushFilesDropped, this,
            [this](const QStringList& paths) { openBrushImportDialog(paths); });

    m_customShapesPanelDock = new QDockWidget(tr("Custom Shapes"), this);
    m_customShapesPanelDock->setObjectName(QStringLiteral("CustomShapesDock"));
    m_customShapesPanelDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    m_customShapesPanelDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    m_customShapesPanel = new CustomShapesPanel(this);
    m_customShapesPanelDock->setWidget(m_customShapesPanel);
    themePanelBg(m_customShapesPanel, thInit);
    addDockWidget(Qt::RightDockWidgetArea, m_customShapesPanelDock);
    m_customShapesPanelDock->setFloating(true);
    m_customShapesPanelDock->resize(380, 520);
    m_customShapesPanelDock->hide();

    connect(m_customShapesPanel, &CustomShapesPanel::iconSelected, this,
            [this](const ShapeIconInfo& icon) {
        if (m_canvas) {
            m_canvas->setCustomShapeIcon(icon);
            m_canvas->setShapeType(static_cast<int>(ShapeToolMode::CustomShape));
            m_canvas->setTool(CanvasView::Tool::Shape);
        }
        if (m_toolBar) {
            m_toolBar->setTool(static_cast<int>(CanvasView::Tool::Shape));
            m_toolBar->setShapeType(static_cast<int>(ShapeToolMode::CustomShape));
        }
        if (m_toolsPanel)
            m_toolsPanel->setActiveTool(static_cast<int>(CanvasView::Tool::Shape));
    });

    auto* customShapesWatcher = new QFutureWatcher<QList<ShapeIconInfo>>(this);
    connect(customShapesWatcher, &QFutureWatcher<QList<ShapeIconInfo>>::finished, this,
            [this, customShapesWatcher]() {
        if (m_customShapesPanel) {
            connect(m_customShapesPanel, &CustomShapesPanel::iconsPrepared, this, [this, customShapesWatcher]() {
                m_customShapeIconPreloadFinished = true;
                emit customShapeIconPreloadFinished();
                customShapesWatcher->deleteLater();
            }, Qt::SingleShotConnection);
            m_customShapesPanel->setIcons(customShapesWatcher->result(), 380);
        } else {
            m_customShapeIconPreloadFinished = true;
            emit customShapeIconPreloadFinished();
            customShapesWatcher->deleteLater();
        }
    });
    customShapesWatcher->setFuture(QtConcurrent::run([]() {
        ShapeIconLibrary library;
        return library.loadBuiltInIcons();
    }));

    m_generativeFillDock = new QDockWidget(tr("Generative Fill"), this);
    m_generativeFillDock->setObjectName("GenerativeFillDock");
    m_generativeFillDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    m_generativeFillDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    m_generativeFillPanel = new GenerativeFillDialog(m_ctrl, m_presetManager, m_generativeFillDock);
    m_generativeFillDock->setWidget(m_generativeFillPanel);
    themePanelBg(m_generativeFillPanel, thInit);
    addDockWidget(Qt::RightDockWidgetArea, m_generativeFillDock);
    m_generativeFillDock->setFloating(true);
    m_generativeFillDock->resize(420, 360);
    m_generativeFillDock->hide();
    connect(m_generativeFillPanel, &GenerativeFillDialog::closeRequested,
            m_generativeFillDock, &QDockWidget::hide);

    // After any float/dock/tab change, unwrap stray single-dock group containers
    // (the white-bordered leftover Qt leaves when a tab is dragged out of a
    // floating group) so they can never be re-merged into a crash.
    for (QDockWidget* d : { m_brushSettingsDock, m_brushPanelDock, m_customShapesPanelDock, m_generativeFillDock,
                            m_layersDock, m_agentDock, m_histogramDock, m_propsDock,
                            m_colorDock, m_swatchesDock, m_historyDock,
                            m_channelsDock }) {
        // connect(d, &QDockWidget::topLevelChanged, this, [this](bool) { scheduleDockMaintenance(); });
        // connect(d, &QDockWidget::visibilityChanged, this, [this](bool) { scheduleDockMaintenance(); });
    }

    // The on-demand panels start floating but can be tabbed into the right-hand
    // column like any other; the ones built by makePanelDock() are already wired.
    for (QDockWidget* d : { m_brushSettingsDock, m_brushPanelDock,
                            m_customShapesPanelDock, m_generativeFillDock })
        watchDockTitleBar(d);

    // Establish the default panel arrangement (also reused by the menu action).
    resetDockLayout();

    // Every dock exists and is registered with QMainWindow by now, which is
    // the precondition for restoreState(): only here can the stored workspace
    // be put back. The default above is what stays on screen when there is
    // nothing stored or the stored blob is rejected.
    m_workspace = new WorkspaceManager(this, this, this);
    m_workspace->restoreWorkingLayout();
    m_workspace->watchDocks(panelDocks());

    connect(m_brushDynamics, &BrushDynamicsPanel::importBrushesRequested, this,
            [this]() { if (m_importBrushesAction) m_importBrushesAction->trigger(); });
    connect(m_brushDynamics, &BrushDynamicsPanel::sizeChanged, this, [this](float s) {
        if (m_canvas) m_canvas->setBrushSize(s);
        if (m_toolBar) m_toolBar->setBrushSize(qRound(s));
        if (m_brushPanel) m_brushPanel->setSize(s);
    });
    connect(m_brushDynamics, &BrushDynamicsPanel::hardnessChanged, this, [this](float h) {
        if (m_canvas) m_canvas->setBrushHardness(h);
        if (m_toolBar) m_toolBar->setBrushHardness(qRound(h * 100.0f));
        if (m_brushPanel) m_brushPanel->setHardness(h);
    });
    connect(m_brushDynamics, &BrushDynamicsPanel::angleChanged, this, [this](float a) {
        if (m_canvas) m_canvas->setBrushAngle(a);
    });
    connect(m_brushDynamics, &BrushDynamicsPanel::roundnessChanged, this, [this](float r) {
        if (m_canvas) m_canvas->setBrushRoundness(r);
    });
    connect(m_brushDynamics, &BrushDynamicsPanel::flipXChanged, this, [this](bool on) {
        if (m_canvas) m_canvas->setBrushFlipX(on);
    });
    connect(m_brushDynamics, &BrushDynamicsPanel::flipYChanged, this, [this](bool on) {
        if (m_canvas) m_canvas->setBrushFlipY(on);
    });
    connect(m_brushDynamics, &BrushDynamicsPanel::colorDynamicsChanged, this, [this](const ColorDynamics& d) {
        if (m_canvas) m_canvas->setBrushColorDynamics(d);
    });
    connect(m_brushDynamics, &BrushDynamicsPanel::textureConfigChanged, this, [this](const TextureConfig& d) {
        if (m_canvas) m_canvas->setBrushTextureConfig(d);
    });
    connect(m_brushDynamics, &BrushDynamicsPanel::dualBrushConfigChanged, this, [this](const DualBrushConfig& d) {
        if (m_canvas) m_canvas->setBrushDualBrushConfig(d);
    });
    connect(m_brushDynamics, &BrushDynamicsPanel::smoothingModeChanged, this, [this](SmoothingMode m) {
        if (m_canvas) m_canvas->setBrushSmoothingMode(m);
    });
    connect(m_brushDynamics, &BrushDynamicsPanel::smoothingRadiusChanged, this, [this](float r) {
        if (m_canvas) m_canvas->setBrushSmoothingRadius(r);
    });
    connect(m_brushDynamics, &BrushDynamicsPanel::spacingChanged, this, [this](float s) {
        if (m_canvas) m_canvas->setBrushSpacing(s);
    });
    connect(m_brushDynamics, &BrushDynamicsPanel::blendModeChanged, this, [this](BrushBlendMode m) {
        if (m_canvas) m_canvas->setBrushBlendMode(m);
    });
    connect(m_brushDynamics, &BrushDynamicsPanel::airbrushChanged, this, [this](bool a) {
        if (m_canvas) m_canvas->setBrushAirbrush(a);
    });
    connect(m_brushDynamics, &BrushDynamicsPanel::wetEdgesChanged, this, [this](bool w) {
        if (m_canvas) m_canvas->setBrushWetEdges(w);
    });
    connect(m_brushDynamics, &BrushDynamicsPanel::tipImageChanged, this, [this](const QImage& tip) {
        if (!m_canvas) return;
        if (tip.isNull())
            m_canvas->setBrushTipSource(BrushTipSource::Circle);
        else
            m_canvas->setBrushTipImage(tip);
    });
    connect(m_brushDynamics, &BrushDynamicsPanel::advancedSettingsChanged, this,
            [this](const BrushSettings& s) {
        if (!m_canvas) return;
        m_canvas->setBrushAutoBrushConfig(s.autoBrush);
        m_canvas->setBrushAutoSpacing(s.useAutoSpacing, s.autoSpacingCoeff);
        m_canvas->setBrushTipRemap(s.tipBrightness, s.tipContrast, s.tipMidpoint);
        m_canvas->setBrushSizeOption(s.sizeOption);
        m_canvas->setBrushOpacityOption(s.opacityOption);
        m_canvas->setBrushFlowOption(s.flowOption);
        m_canvas->setBrushRotationOption(s.rotationOption);
        m_canvas->setBrushRatioOption(s.ratioOption);
        m_canvas->setBrushScatter(s.scatterOption, s.scatter);
        m_canvas->setBrushAirbrushRate(s.airbrushRate);
        // Mirror the pressure state to the quick toggle + action; the fine
        // controls live exclusively in this panel.
        const bool pressureOn = brushPressureEnabled(s);
        if (m_toolBar) m_toolBar->setBrushPressureEnabled(pressureOn);
        if (m_brushPressureAction) {
            QSignalBlocker b(m_brushPressureAction);
            m_brushPressureAction->setChecked(pressureOn);
        }
    });
    connect(m_brushDynamics, &BrushDynamicsPanel::presetSelected, this, [this](const BrushPreset& preset) {
        if (!m_canvas) return;
        const auto& s = preset.settings;
        m_canvas->setBrushSize(s.size);
        m_canvas->setBrushHardness(s.hardness);
        m_canvas->setBrushOpacity(s.opacity);
        m_canvas->setBrushFlow(s.flow);
        m_canvas->setBrushType(s.type);
        m_canvas->setBrushApplication(s.application);
        m_canvas->setBrushPaintMode(s.paintMode);
        if (s.tipSource == BrushTipSource::Image) {
            QImage tip;
            if (!s.tipImageData.isEmpty())
                tip.loadFromData(QByteArray::fromBase64(s.tipImageData.toUtf8()));
            else if (!s.tipImagePath.isEmpty())
                tip.load(s.tipImagePath);
            if (!tip.isNull())
                m_canvas->setBrushTipImage(tip);
            else
                m_canvas->setBrushTipSource(BrushTipSource::Circle);
        } else {
            m_canvas->setBrushTipSource(BrushTipSource::Circle);
        }
        m_canvas->setBrushAngle(s.angle);
        m_canvas->setBrushRoundness(s.roundness);
        m_canvas->setBrushFlipX(s.flipX);
        m_canvas->setBrushFlipY(s.flipY);
        m_canvas->setBrushScatter(s.scatterOption, s.scatter);
        m_canvas->setBrushColorDynamics(s.colorDynamics);
        m_canvas->setBrushTextureConfig(s.textureConfig);
        m_canvas->setBrushDualBrushConfig(s.dualBrushConfig);
        m_canvas->setBrushBlendMode(s.blendMode);
        m_canvas->setBrushSmoothingMode(s.smoothingMode);
        m_canvas->setBrushSmoothingRadius(s.smoothingRadius);
        m_canvas->setBrushSpacing(s.spacing);
        m_canvas->setBrushAirbrush(s.airbrush);
        m_canvas->setBrushAirbrushRate(s.airbrushRate);
        m_canvas->setBrushWetEdges(s.wetEdges);
        m_canvas->setBrushAutoBrushConfig(s.autoBrush);
        m_canvas->setBrushAutoSpacing(s.useAutoSpacing, s.autoSpacingCoeff);
        m_canvas->setBrushTipRemap(s.tipBrightness, s.tipContrast, s.tipMidpoint);
        m_canvas->setBrushSizeOption(s.sizeOption);
        m_canvas->setBrushOpacityOption(s.opacityOption);
        m_canvas->setBrushFlowOption(s.flowOption);
        m_canvas->setBrushRotationOption(s.rotationOption);
        m_canvas->setBrushRatioOption(s.ratioOption);
        // Pressure state follows the curve options.
        const bool pressureOn = brushPressureEnabled(s);
        if (m_toolBar) m_toolBar->setBrushPressureEnabled(pressureOn);
        if (m_brushPressureAction) {
            QSignalBlocker b(m_brushPressureAction);
            m_brushPressureAction->setChecked(pressureOn);
        }
        if (m_brushPanel) m_brushPanel->setCurrentPreset(preset.name);
    });

    connect(m_brushDynamics, &BrushDynamicsPanel::presetSaveRequested, this, [this]() {
        // Handled internally by BrushDynamicsPanel
    });

    connect(m_agentPanel, &AgentPanel::agentSelected,
            this, &MainWindow::onAgentSelected);
    connect(m_agentPanel, &AgentPanel::configureClicked,
            this, &MainWindow::onConfigureAgent);

    // ── History Panel ──
    connect(m_historyPanel, &HistoryPanel::jumpRequested, this, [this](int idx) {
        if (!m_ctrl) return;
        m_ctrl->jumpToHistoryState(idx);
        m_canvas->syncLayersToGpu();
        m_canvas->update();
        refreshLayerPanel();
        refreshPropertiesPanel();
        updateLayerMenuState();
    });
    connect(m_historyPanel, &HistoryPanel::undoRequested, this, &MainWindow::onUndo);
    connect(m_historyPanel, &HistoryPanel::redoRequested, this, &MainWindow::onRedo);
    connect(m_historyPanel, &HistoryPanel::clearRequested, this, [this]() {
        if (m_ctrl) {
            m_ctrl->clearHistory();
            refreshHistoryPanel();
        }
    });

    setupAgents();

    m_statusMessageTimer = new QTimer(this);
    m_statusMessageTimer->setSingleShot(true);
    connect(m_statusMessageTimer, &QTimer::timeout, this, [this]() {
        if (m_statusMessageLabel)
            m_statusMessageLabel->setText(tr("Ready"));
        if (m_statusIconLabel) {
            m_statusIconLabel->clear();
            m_statusIconLabel->setVisible(false);
        }
    });

    connect(m_zoomCombo, &ZoomComboBox::zoomRequested, this, [this](float zoom) {
        if (m_canvas)
            m_canvas->setZoom(zoom);
        else if (m_zoomCombo)
            m_zoomCombo->setZoom(1.0f);
    });

    connect(m_toolsPanel, &ToolsPanel::toolSelected, this, &MainWindow::onToolSelected);
    connect(m_toolsPanel, &ToolsPanel::toolGroupActivated,
            this, [this](int tool, const QString&, int activeSubTool, const QVector<int>& subTools) {
        clearSelectRefinePreview();
        int canvasTool = tool;
        if (tool == static_cast<int>(CanvasView::Tool::FillBucket)
            && activeSubTool == static_cast<int>(CanvasView::Tool::Gradient)) {
            canvasTool = static_cast<int>(CanvasView::Tool::Gradient);
        }
        // Move flyout hosts the independent Skew tool (like Fill Bucket hosts
        // Gradient): the Skew sub-tool maps to its own canvas tool.
        if (tool == static_cast<int>(CanvasView::Tool::Move)
            && activeSubTool == static_cast<int>(ToolsPanel::Skew)) {
            canvasTool = static_cast<int>(CanvasView::Tool::Skew);
        }
        // Temporarily hidden: AI Remove Tool will be released later.
        // Do not remove the implementation. Only the user-facing UI is disabled.
        /*
        if (tool == static_cast<int>(CanvasView::Tool::AiSelect)
            && activeSubTool == static_cast<int>(ToolsPanel::AiRemove)) {
            canvasTool = static_cast<int>(CanvasView::Tool::AiRemove);
        }
        */
        if (canvasTool == static_cast<int>(CanvasView::Tool::AiSelect)
            && !guardAiObjectSelectionActivation())
            return;
        // Temporarily hidden: AI Remove Tool will be released later.
        // Do not remove the implementation. Only the user-facing UI is disabled.
        /*
        if (canvasTool == static_cast<int>(CanvasView::Tool::AiRemove)
            && !guardAiRemoveActivation())
            return;
        */
        if (m_canvas) {
            // Clone Stamp flyout: the sub-tool selects the stamp mode (Clone/
            // Healing); set it before setTool so toolChanged syncs the options page.
            if (tool == static_cast<int>(CanvasView::Tool::CloneStamp))
                m_canvas->setStampMode(activeSubTool);
            m_canvas->setTool(static_cast<CanvasView::Tool>(canvasTool));
            if (tool == static_cast<int>(CanvasView::Tool::Select))
                m_canvas->setSelectType(static_cast<SelectType>(activeSubTool));
        } else if (m_toolBar) {
            m_toolBar->setTool(canvasTool);
        }

        if (m_toolBar) {
            m_toolBar->setSubToolsForTool(tool, subTools, activeSubTool);
            if (canvasTool == static_cast<int>(CanvasView::Tool::AiSelect) && m_canvas) {
                m_toolBar->setAiToolPageMode(AiToolPageMode::ObjectSelection);
                m_toolBar->bindAiController(m_canvas->aiSelectionController());
                ensureAiControllerConnected();
            }
            // Temporarily hidden: AI Remove Tool will be released later.
            // Do not remove the implementation. Only the user-facing UI is disabled.
            /*
            else if (canvasTool == static_cast<int>(CanvasView::Tool::AiRemove) && m_canvas) {
                m_toolBar->setAiToolPageMode(AiToolPageMode::RemoveObject);
                m_toolBar->bindAiRemoveController(m_canvas->aiRemoveController());
            }
            */
            m_toolBar->setVisible(true);
        }
        updateToolBarLayout();
        updateDistortControls();
    });

    connect(m_layerPanel, &LayerPanel::addLayerRequested, this, &MainWindow::onAddLayer);
    connect(m_layerPanel, &LayerPanel::removeLayerRequested, this, &MainWindow::onRemoveLayer);
    connect(m_layerPanel, &LayerPanel::activeLayerChanged, this, &MainWindow::onActiveLayerChanged);
    connect(m_layerPanel, &LayerPanel::grayscaleViewRequested, this, [this](bool) {
        if (m_canvas) m_canvas->setGrayscaleMaskView(!m_canvas->isGrayscaleMaskView());
    });
    connect(m_layerPanel, &LayerPanel::layerStylesRequested,
            this, &MainWindow::showLayerStylesDialog);
    connect(m_layerPanel, &LayerPanel::aiUpscaleRequested,
            this, [this](int flatIndex) {
        if (!m_ctrl) return;
        m_ctrl->setActiveNode(flatIndex);
        showAiUpscaleDialog(UpscaleTarget::CurrentLayer);
    });
    connect(m_layerPanel, &LayerPanel::exportNodeRequested,
            this, &MainWindow::onExportNode);
    connect(m_layerPanel, &LayerPanel::solidColorRequested,
            this, &MainWindow::createSolidColorLayer);
    connect(m_layerPanel, &LayerPanel::solidColorEditRequested,
            this, [this](int flatIndex) {
        if (!m_ctrl) return;
        m_ctrl->setActiveNode(flatIndex);    // select the layer in the panel/canvas
        if (m_propsPanel) {
            // Bind + show the Solid Color editor explicitly (don't rely on the
            // Properties dock being visible), then open its picker.
            m_propsPanel->showSolidColorEditor(flatIndex);
            m_propsPanel->openSolidColorPicker();
        }
    });
    connect(m_layerPanel, &LayerPanel::rawEditRequested,
            this, [this](int flatIndex) { openHazorDevelop(flatIndex); });
    connect(m_layerPanel, &LayerPanel::smartObjectEditRequested,
            this, [this](int flatIndex) { editSmartObjectContents(flatIndex); });
    connect(m_layerPanel, &LayerPanel::smartObjectCommandRequested,
            this, &MainWindow::runSmartObjectCommand);

    connect(m_toolBar, &ToolOptionsBar::aiOpenSettingsRequested, this, &MainWindow::onOpenAiSettings);
    connect(m_toolBar, &ToolOptionsBar::brushSizeChanged, this, &MainWindow::onBrushSizeChanged);
    connect(m_toolBar, &ToolOptionsBar::brushOpacityChanged, this, &MainWindow::onBrushOpacityChanged);
    connect(m_toolBar, &ToolOptionsBar::brushHardnessChanged, this, &MainWindow::onBrushHardnessChanged);
    connect(m_toolBar, &ToolOptionsBar::brushFlowChanged, this, &MainWindow::onBrushFlowChanged);
    connect(m_toolBar, &ToolOptionsBar::brushMinPressureChanged, this, [this](int percent) {
        if (m_canvas) m_canvas->setBrushMinPressure(static_cast<float>(percent) / 100.0f);
    });
    connect(m_toolBar, &ToolOptionsBar::brushColorChanged, this, &MainWindow::onBrushColorChanged);

    // Pen-pressure quick toggle (Options Bar) + a QAction so a shortcut / menu /
    // command-palette entry can drive the same master switch later. Both route
    // through applyBrushPressureEnabled, the single non-looping sync point.
    m_brushPressureAction = new QAction(tr("Use Pen Pressure"), this);
    m_brushPressureAction->setCheckable(true);
    m_brushPressureAction->setChecked(m_canvas ? m_canvas->brushPressureEnabled() : true);
    addAction(m_brushPressureAction);
    connect(m_brushPressureAction, &QAction::toggled, this,
            [this](bool on) { applyBrushPressureEnabled(on); });
    connect(m_toolBar, &ToolOptionsBar::brushPressureToggled, this,
            [this](bool on) { applyBrushPressureEnabled(on); });
    connect(m_toolBar, &ToolOptionsBar::cloneAlignedChanged, this, [this](bool aligned) {
        if (m_canvas) m_canvas->setCloneAligned(aligned);
    });
    connect(m_toolBar, &ToolOptionsBar::cloneSampleModeChanged, this, [this](int mode) {
        if (m_canvas) m_canvas->setCloneSampleMode(mode);
    });
    connect(m_toolBar, &ToolOptionsBar::cloneBlendModeChanged, this, [this](int mode) {
        if (m_canvas) m_canvas->setBrushBlendMode(static_cast<BrushBlendMode>(mode));
    });
    connect(m_toolBar, &ToolOptionsBar::healingDiffusionChanged, this, [this](int diffusion) {
        if (m_canvas) m_canvas->setHealingDiffusion(diffusion / 100.0f);
    });
    connect(m_toolBar, &ToolOptionsBar::presetSelected, this, [this](const BrushPreset& preset) {
        if (!m_canvas) return;
        const auto& s = preset.settings;
        m_canvas->setBrushSize(s.size);
        m_canvas->setBrushHardness(s.hardness);
        m_canvas->setBrushOpacity(s.opacity);
        m_canvas->setBrushFlow(s.flow);
        m_canvas->setBrushType(s.type);
        m_canvas->setBrushApplication(s.application);
        m_canvas->setBrushPaintMode(s.paintMode);
        if (s.tipSource == BrushTipSource::Image) {
            QImage tip;
            if (!s.tipImageData.isEmpty())
                tip.loadFromData(QByteArray::fromBase64(s.tipImageData.toUtf8()));
            else if (!s.tipImagePath.isEmpty())
                tip.load(s.tipImagePath);
            if (!tip.isNull())
                m_canvas->setBrushTipImage(tip);
            else
                m_canvas->setBrushTipSource(BrushTipSource::Circle);
        } else {
            m_canvas->setBrushTipSource(BrushTipSource::Circle);
        }
        m_canvas->setBrushAngle(s.angle);
        m_canvas->setBrushRoundness(s.roundness);
        m_canvas->setBrushFlipX(s.flipX);
        m_canvas->setBrushFlipY(s.flipY);
        m_canvas->setBrushScatter(s.scatterOption, s.scatter);
        m_canvas->setBrushColorDynamics(s.colorDynamics);
        m_canvas->setBrushTextureConfig(s.textureConfig);
        m_canvas->setBrushDualBrushConfig(s.dualBrushConfig);
        m_canvas->setBrushBlendMode(s.blendMode);
        m_canvas->setBrushSmoothingMode(s.smoothingMode);
        m_canvas->setBrushSmoothingRadius(s.smoothingRadius);
        m_canvas->setBrushSpacing(s.spacing);
        m_canvas->setBrushAirbrush(s.airbrush);
        m_canvas->setBrushAirbrushRate(s.airbrushRate);
        m_canvas->setBrushWetEdges(s.wetEdges);
        m_canvas->setBrushAutoBrushConfig(s.autoBrush);
        m_canvas->setBrushAutoSpacing(s.useAutoSpacing, s.autoSpacingCoeff);
        m_canvas->setBrushTipRemap(s.tipBrightness, s.tipContrast, s.tipMidpoint);
        m_canvas->setBrushSizeOption(s.sizeOption);
        m_canvas->setBrushOpacityOption(s.opacityOption);
        m_canvas->setBrushFlowOption(s.flowOption);
        m_canvas->setBrushRotationOption(s.rotationOption);
        m_canvas->setBrushRatioOption(s.ratioOption);
        // Pressure state follows the curve options.
        const bool pressureOn = brushPressureEnabled(s);
        m_toolBar->setBrushPressureEnabled(pressureOn);
        if (m_brushPressureAction) {
            QSignalBlocker b(m_brushPressureAction);
            m_brushPressureAction->setChecked(pressureOn);
        }
        m_brushDynamics->setFromSettings(s);
        if (m_brushPanel) m_brushPanel->setCurrentPreset(preset.name);
    });
    connect(m_toolBar, &ToolOptionsBar::foregroundColorChanged, this, [this](const QColor& color) {
        if (!m_colorEngine) return;
        m_colorEngine->setForegroundColor(color);
    });

    connect(m_toolBar, &ToolOptionsBar::fillBucketToleranceChanged, this, [this](int value) {
        if (m_canvas) m_canvas->setFillBucketTolerance(value);
    });
    connect(m_toolBar, &ToolOptionsBar::fillBucketColorChanged, this, [this](const QColor& color) {
        if (m_canvas) m_canvas->setFillBucketColor(color);
        if (m_colorEngine) m_colorEngine->setForegroundColor(color);
    });
    connect(m_toolBar, &ToolOptionsBar::gradientDefinitionChanged, this, [this](const GradientDefinition& definition) {
        if (m_canvas) m_canvas->setGradientDefinition(definition);
    });
    connect(m_toolBar, &ToolOptionsBar::gradientBlendModeChanged, this, [this](int mode) {
        if (m_canvas) m_canvas->setGradientBlendMode(static_cast<BlendMode>(mode));
    });
    connect(m_toolBar, &ToolOptionsBar::gradientOpacityChanged, this, [this](int opacity) {
        if (m_canvas) m_canvas->setGradientOpacity(std::clamp(opacity / 100.0f, 0.0f, 1.0f));
    });

    connect(m_toolBar, &ToolOptionsBar::brushSettingsRequested, this, [this]() {
        if (!m_brushDynamics) return;
        auto* dock = qobject_cast<QDockWidget*>(m_brushDynamics->parentWidget());
        if (dock) {
            dock->setFloating(true);
            auto* btn = m_toolBar->brushSettingsButton();
            if (btn) {
                QPoint pos = btn->mapToGlobal(QPoint(0, btn->height()));
                dock->move(pos);
            }
            dock->show();
            dock->raise();
        }
    });

    connect(m_toolBar, &ToolOptionsBar::openBrushPanelRequested,
            this, &MainWindow::showBrushPanel);
    connect(m_toolBar, &ToolOptionsBar::importBrushesRequested, this,
            [this]() { if (m_importBrushesAction) m_importBrushesAction->trigger(); });
    connect(m_toolBar, &ToolOptionsBar::openCustomShapesPanelRequested,
            this, &MainWindow::showCustomShapesPanel);

    connect(m_toolBar, &ToolOptionsBar::textFontChanged, this, &MainWindow::onTextFontChanged);
    connect(m_toolBar, &ToolOptionsBar::textSizeChanged, this, &MainWindow::onTextSizeChanged);
    connect(m_toolBar, &ToolOptionsBar::textBoldChanged, this, &MainWindow::onTextBoldChanged);
    connect(m_toolBar, &ToolOptionsBar::textItalicChanged, this, &MainWindow::onTextItalicChanged);
    connect(m_toolBar, &ToolOptionsBar::textUnderlineChanged, this, &MainWindow::onTextUnderlineChanged);
    connect(m_toolBar, &ToolOptionsBar::textStrikethroughChanged, this, &MainWindow::onTextStrikethroughChanged);
    connect(m_toolBar, &ToolOptionsBar::textColorChanged, this, &MainWindow::onTextColorChanged);
    // The text colour dialog previews on the canvas while it is open: every
    // intermediate colour repaints the selected text undo-lessly, and the one
    // undo step (or the restore) lands when the dialog closes.
    connect(m_toolBar, &ToolOptionsBar::textColorPreviewChanged, this, [this](const QColor& c) {
        if (m_canvas) m_canvas->previewTextColor(c);
    });
    connect(m_toolBar, &ToolOptionsBar::textColorPreviewFinished, this, [this](bool keep) {
        if (!m_canvas) return;
        m_canvas->endTextColorPreview(keep);
        refreshLayerPanel();
        refreshHistoryPanel();
    });
    connect(m_toolBar, &ToolOptionsBar::textAlignChanged, this, &MainWindow::onTextAlignChanged);
    connect(m_toolBar, &ToolOptionsBar::textTrackingChanged, this, &MainWindow::onTextTrackingChanged);
    connect(m_toolBar, &ToolOptionsBar::textLeadingChanged, this, &MainWindow::onTextLeadingChanged);

    connect(m_toolBar, &ToolOptionsBar::zoomFitClicked, this, [this]() {
        if (m_canvas) m_canvas->fitToView();
    });
    connect(m_toolBar, &ToolOptionsBar::zoomOriginalClicked, this, [this]() {
        if (m_canvas) m_canvas->zoomToOriginal();
    });
    connect(m_statusZoomFitBtn, &QPushButton::clicked, this, [this]() {
        if (m_canvas) m_canvas->fitToView();
    });
    connect(m_statusZoom100Btn, &QPushButton::clicked, this, [this]() {
        if (m_canvas) m_canvas->zoomToOriginal();
    });

    connect(m_colorEngine, &ColorEngine::foregroundColorChanged, this, [this](const QColor& color) {
        if (m_toolBar) m_toolBar->setForegroundColor(color);
        if (m_canvas) m_canvas->setBrushColor(color);
        if (m_canvas) m_canvas->setFillBucketColor(color);
        if (m_canvas) m_canvas->setShapeFillColor(color);
        // A text layer's color is only recolored by the foreground swatch when the
        // Move tool is active. Otherwise picking a brush color while a text layer
        // happens to be selected must not repaint it.
        const CanvasView::Tool tool = m_canvas ? m_canvas->currentTool() : CanvasView::Tool::Move;
        if (m_canvas && tool == CanvasView::Tool::Move)
            m_canvas->setTextColor(color);
        // behavior: with Move tool and a shape selected,
        // foreground updates shape fill directly.
        if (m_doc && m_ctrl && m_canvas && tool == CanvasView::Tool::Move) {
            const int activeIndex = m_doc->activeFlatIndex;
            auto* node = m_doc->nodeAt(activeIndex);
            if (node && node->layer && node->layer->shapeData) {
                ShapeData data = *node->layer->shapeData;
                data.style.fillColor = color;
                m_ctrl->modifyShapeLayer(activeIndex, data);
            }
        }
    });
    connect(m_colorEngine, &ColorEngine::backgroundColorChanged, this, [this](const QColor& color) {
        if (m_toolBar) m_toolBar->setBackgroundColor(color);
        if (m_canvas) m_canvas->setShapeStrokeColor(color);
        // behavior: with Move tool and a shape selected,
        // background updates shape stroke directly.
        const CanvasView::Tool tool = m_canvas ? m_canvas->currentTool() : CanvasView::Tool::Move;
        if (m_doc && m_ctrl && m_canvas && tool == CanvasView::Tool::Move) {
            const int activeIndex = m_doc->activeFlatIndex;
            auto* node = m_doc->nodeAt(activeIndex);
            if (node && node->layer && node->layer->shapeData) {
                ShapeData data = *node->layer->shapeData;
                data.style.strokeColor = color;
                m_ctrl->modifyShapeLayer(activeIndex, data);
            }
        }
    });
    connect(m_colorPaletteBar, &ColorPaletteBar::foregroundColorSelected, this, [this](const QColor& color) {
        if (!m_colorEngine) return;
        m_colorEngine->setForegroundColor(color);
    });
    connect(m_colorPaletteBar, &ColorPaletteBar::backgroundColorSelected, this, [this](const QColor& color) {
        if (!m_colorEngine) return;
        m_colorEngine->setBackgroundColor(color);
    });

    // color.swap is owned by the ShortcutManager dispatcher (callback below), not
    // a QShortcut — so the Crop modal's X can outrank it by context. (color.reset
    // stays a global QShortcut for now; D is reserved for the future Skew work.)
    auto* defaultShortcut = new QShortcut(this);
    ShortcutManager::instance()->registerShortcut("color.reset_default", defaultShortcut);
    connect(defaultShortcut, &QShortcut::activated, this, [this]() {
        if (!m_colorEngine) return;
        m_colorEngine->resetDefaultColors();
    });

    m_toolBar->setForegroundColor(m_colorEngine->foregroundColor());
    m_toolBar->setBackgroundColor(m_colorEngine->backgroundColor());
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        if (m_colorEngine) m_colorEngine->save();
        SwatchManager::instance()->save();
    });

    connect(m_toolBar, &ToolOptionsBar::eyedropperSampleModeChanged, this, [this](int mode) {
        if (m_canvas) m_canvas->setEyedropperSampleMode(static_cast<SampleMode>(mode));
    });
    connect(m_toolBar, &ToolOptionsBar::eyedropperSampleSizeChanged, this, [this](int size) {
        if (m_canvas) m_canvas->setEyedropperSampleSize(static_cast<SampleSize>(size));
    });

    // Shape tool connections
    auto updateActiveShape = [this](auto mutate) {
        if (!m_doc || !m_ctrl) return;
        const int activeIndex = m_doc->activeFlatIndex;
        auto* node = m_doc->nodeAt(activeIndex);
        if (!node || !node->layer || !node->layer->shapeData) return;
        ShapeData data = *node->layer->shapeData;
        mutate(data);
        m_ctrl->modifyShapeLayer(activeIndex, data);
    };

    connect(m_toolBar, &ToolOptionsBar::shapeTypeChanged, this, [this](int type) {
        if (m_canvas) m_canvas->setShapeType(type);
    });
    // Same mutation as updateActiveShape, but through the controller's preview
    // session: a colour dialog repaints the selected shape on every intermediate
    // value and still leaves a single undo step behind.
    auto previewActiveShape = [this](auto mutate) {
        if (!m_doc || !m_ctrl) return;
        const int activeIndex = m_doc->activeFlatIndex;
        auto* node = m_doc->nodeAt(activeIndex);
        if (!node || !node->layer || !node->layer->shapeData) return;
        m_ctrl->beginShapeDataPreview(activeIndex);
        ShapeData data = *node->layer->shapeData;
        mutate(data);
        m_ctrl->previewShapeData(data);
    };

    connect(m_toolBar, &ToolOptionsBar::shapeFillColorChanged, this, [this, updateActiveShape](const QColor& c) {
        if (m_canvas) m_canvas->setShapeFillColor(c);
        // A live preview already applied this colour and commits its own single
        // undo step when the dialog closes; editing again here would duplicate
        // the history entry.
        if (m_ctrl && m_ctrl->shapeDataPreviewActive()) return;
        updateActiveShape([&](ShapeData& data) {
            data.style.fillEnabled = true;
            data.style.fillColor = c;
        });
    });
    connect(m_toolBar, &ToolOptionsBar::shapeFillColorPreviewChanged, this,
            [this, previewActiveShape](const QColor& c) {
        if (m_canvas) m_canvas->setShapeFillColor(c);
        previewActiveShape([&](ShapeData& data) {
            data.style.fillEnabled = true;
            data.style.fillColor = c;
        });
    });
    connect(m_toolBar, &ToolOptionsBar::shapeFillEnabledChanged, this, [this, updateActiveShape](bool enabled) {
        if (m_canvas) m_canvas->setShapeFillEnabled(enabled);
        updateActiveShape([&](ShapeData& data) { data.style.fillEnabled = enabled; });
    });
    connect(m_toolBar, &ToolOptionsBar::shapeStrokeColorChanged, this, [this, updateActiveShape](const QColor& c) {
        if (m_canvas) m_canvas->setShapeStrokeColor(c);
        if (m_ctrl && m_ctrl->shapeDataPreviewActive()) return;
        updateActiveShape([&](ShapeData& data) {
            data.style.strokeEnabled = true;
            data.style.strokeColor = c;
        });
    });
    connect(m_toolBar, &ToolOptionsBar::shapeStrokeColorPreviewChanged, this,
            [this, previewActiveShape](const QColor& c) {
        if (m_canvas) m_canvas->setShapeStrokeColor(c);
        previewActiveShape([&](ShapeData& data) {
            data.style.strokeEnabled = true;
            data.style.strokeColor = c;
        });
    });
    connect(m_toolBar, &ToolOptionsBar::shapeColorPreviewFinished, this, [this](bool keep) {
        if (m_ctrl) m_ctrl->endShapeDataPreview(keep);
        if (m_canvas) { m_canvas->syncLayersToGpu(); m_canvas->update(); }
        refreshLayerPanel();
        refreshHistoryPanel();
    });
    connect(m_toolBar, &ToolOptionsBar::shapeStrokeEnabledChanged, this, [this, updateActiveShape](bool enabled) {
        if (m_canvas) m_canvas->setShapeStrokeEnabled(enabled);
        updateActiveShape([&](ShapeData& data) { data.style.strokeEnabled = enabled; });
    });
    connect(m_toolBar, &ToolOptionsBar::shapeStrokeWidthChanged, this, [this, updateActiveShape](int w) {
        if (m_canvas) {
            float ndcW = static_cast<float>(w) / (m_doc ? m_doc->size.width() : 500.0f) * 2.0f;
            m_canvas->setShapeStrokeWidth(ndcW);
            updateActiveShape([&](ShapeData& data) { data.style.strokeWidth = ndcW; });
        }
    });
    connect(m_toolBar, &ToolOptionsBar::shapeOpacityChanged, this, [this](int opacity) {
        if (m_canvas) m_canvas->setShapeOpacity(static_cast<float>(opacity) / 100.0f);
        if (!m_doc || !m_ctrl) return;
        const int activeIndex = m_doc->activeFlatIndex;
        auto* node = m_doc->nodeAt(activeIndex);
        if (node && node->layer && node->layer->shapeData)
            m_ctrl->setNodeOpacity(activeIndex, static_cast<float>(opacity) / 100.0f);
    });
    connect(m_toolBar, &ToolOptionsBar::shapeAntiAliasChanged, this, [this, updateActiveShape](bool aa) {
        if (m_canvas) m_canvas->setShapeAntiAlias(aa);
        updateActiveShape([&](ShapeData& data) { data.style.antiAlias = aa; });
    });
    connect(m_toolBar, &ToolOptionsBar::shapeCornerRadiusChanged, this, [this, updateActiveShape](int r) {
        if (m_canvas) {
            float ndcR = static_cast<float>(r) / (m_doc ? m_doc->size.width() : 500.0f) * 2.0f;
            m_canvas->setShapeCornerRadius(ndcR);
            updateActiveShape([&](ShapeData& data) {
                if (data.metadata.presetId == QLatin1String("rectangle")) {
                    const QRectF localBounds = data.path.localBounds.isNull()
                        ? QRectF(0.0, 0.0, 1.0, 1.0)
                        : data.path.localBounds;
                    // ndcR is a canvas-space radius; build circular corners that
                    // account for the (possibly non-uniform) localToCanvas scale
                    // and the document's aspect (canvas->pixel) anisotropy.
                    const QPointF canvasToPixelScale(
                        std::max(1, m_doc ? m_doc->size.width() : 1) * 0.5,
                        std::max(1, m_doc ? m_doc->size.height() : 1) * 0.5);
                    data.path = ndcR > 0.0
                        ? ShapePresetFactory::createRoundedRectangleForTransform(
                              localBounds, ndcR, data.transform.localToCanvas,
                              canvasToPixelScale)
                        : ShapePresetFactory::createRectangle(localBounds);
                    data.metadata.parameters.insert(QStringLiteral("cornerRadius"), ndcR);
                    data.metadata.parametricEditable = true;
                }
            });
        }
    });
    connect(m_toolBar, &ToolOptionsBar::shapeSidesChanged, this, [this, updateActiveShape](int sides) {
        if (m_canvas) m_canvas->setShapeSides(sides);
        updateActiveShape([&](ShapeData& data) {
            const QRectF localBounds = data.path.localBounds.isNull()
                ? QRectF(0.0, 0.0, 1.0, 1.0)
                : data.path.localBounds;
            if (data.metadata.presetId == QLatin1String("polygon")) {
                data.path = ShapePresetFactory::createPolygon(localBounds, sides);
                data.metadata.parameters.insert(QStringLiteral("sides"), sides);
                data.metadata.parametricEditable = true;
            } else if (data.metadata.presetId == QLatin1String("star")) {
                StarOptions options;
                options.points = sides;
                data.path = ShapePresetFactory::createStar(localBounds, options);
                data.metadata.parameters.insert(QStringLiteral("points"), sides);
                data.metadata.parametricEditable = true;
            }
        });
    });

    connect(m_toolBar, &ToolOptionsBar::moveAutoSelectChanged, this, [this](bool enabled) {
        if (m_canvas) m_canvas->setAutoSelect(enabled);
    });
    connect(m_toolBar, &ToolOptionsBar::moveAutoSelectTargetChanged, this, [this](int target) {
        if (m_canvas) m_canvas->setAutoSelectGroup(target == 1);
    });
    connect(m_toolBar, &ToolOptionsBar::moveShowTransformControlsChanged, this, [this](bool show) {
        if (m_canvas) m_canvas->setShowTransformControls(show);
    });
    connect(m_toolBar, &ToolOptionsBar::distortClicked, this, [this]() {
        if (!m_canvas) return;
        // Distort/Perspective are internal modes of the Skew tool: the buttons
        // switch the mode without changing the active tool or ending the session.
        m_canvas->beginDistort(TransformMode::Distort);
        updateDistortControls();
    });
    connect(m_toolBar, &ToolOptionsBar::perspectiveClicked, this, [this]() {
        if (!m_canvas) return;
        m_canvas->beginDistort(TransformMode::Perspective);
        updateDistortControls();
    });
    connect(m_toolBar, &ToolOptionsBar::distortResetClicked, this, [this]() {
        if (m_canvas) m_canvas->resetDistort();
        updateDistortControls();
    });
    connect(m_toolBar, &ToolOptionsBar::distortApplyClicked, this, [this]() {
        // Commit the current transform and leave edit mode.
        if (m_canvas) m_canvas->commitDistort();
        updateDistortControls();
    });
    connect(m_toolBar, &ToolOptionsBar::transformFieldEdited, this,
            &MainWindow::applyPropertiesTransformEdit);
    connect(m_toolBar, &ToolOptionsBar::transformCancelClicked, this, [this]() {
        if (m_canvas) m_canvas->cancelFreeTransform();
    });
    connect(m_toolBar, &ToolOptionsBar::transformApplyClicked, this, [this]() {
        if (m_canvas) m_canvas->commitFreeTransform();
    });
    // Options-bar Align bar removed; the Properties panel's AlignBar drives
    // alignment instead (see PropertiesPanel::alignRequested / alignTargetChanged
    // / resetTransformRequested connections above). m_alignBar is never created.
    // connect(m_alignBar, &AlignBar::alignLeftClicked, this, [this]() {
    //     if (m_canvas) m_canvas->alignLeft();
    // });
    // connect(m_alignBar, &AlignBar::alignCenterHClicked, this, [this]() {
    //     if (m_canvas) m_canvas->alignCenterH();
    // });
    // connect(m_alignBar, &AlignBar::alignRightClicked, this, [this]() {
    //     if (m_canvas) m_canvas->alignRight();
    // });
    // connect(m_alignBar, &AlignBar::alignTopClicked, this, [this]() {
    //     if (m_canvas) m_canvas->alignTop();
    // });
    // connect(m_alignBar, &AlignBar::alignMiddleVClicked, this, [this]() {
    //     if (m_canvas) m_canvas->alignMiddleV();
    // });
    // connect(m_alignBar, &AlignBar::alignBottomClicked, this, [this]() {
    //     if (m_canvas) m_canvas->alignBottom();
    // });
    // connect(m_alignBar, &AlignBar::alignCenterClicked, this, [this]() {
    //     if (!m_canvas) return;
    //     m_canvas->alignCenterH();
    //     m_canvas->alignMiddleV();
    // });
    // connect(m_alignBar, &AlignBar::alignTargetChanged, this, [this](int target) {
    //     if (m_canvas) m_canvas->setAlignTarget(target);
    // });
    // connect(m_alignBar, &AlignBar::resetTransformClicked, this, [this]() {
    //     ...
    // });
    connect(m_toolBar, &ToolOptionsBar::selectTypeChanged, this, [this](int t) {
        if (m_toolsPanel) {
            m_toolsPanel->setActiveSubTool(static_cast<int>(CanvasView::Tool::Select), t);
            m_toolBar->setSubToolsForTool(static_cast<int>(CanvasView::Tool::Select),
                                          m_toolsPanel->activeSubToolsForTool(static_cast<int>(CanvasView::Tool::Select)),
                                          t);
        }
        if (m_canvas) m_canvas->setSelectType(static_cast<SelectType>(t));
    });
    connect(m_toolBar, &ToolOptionsBar::toleranceChanged, this, [this](int v) {
        if (m_canvas) m_canvas->setQuickSelectTolerance(static_cast<float>(v));
    });
    connect(m_toolBar, &ToolOptionsBar::selectModeChanged, this, [this](int m) {
        if (m_canvas) m_canvas->setSelectMode(static_cast<SelectMode>(m));
    });
    connect(m_toolBar, &ToolOptionsBar::selectAntiAliasChanged, this, [this](bool enabled) {
        if (m_canvas) m_canvas->setSelectAntiAlias(enabled);
    });
    connect(m_toolBar, &ToolOptionsBar::selectAllClicked, this, [this]() {
        if (m_ctrl) m_ctrl->executeTool("select_all", {});
    });
    connect(m_toolBar, &ToolOptionsBar::deselectClicked, this, [this]() {
        if (m_ctrl) m_ctrl->executeTool("deselect", {});
    });
    connect(m_toolBar, &ToolOptionsBar::invertClicked, this, [this]() {
        if (m_ctrl) m_ctrl->executeTool("select_invert", {});
    });
    connect(m_toolBar, &ToolOptionsBar::generativeFillClicked,
            this, &MainWindow::onGenerativeFill);
    connect(m_toolBar, &ToolOptionsBar::cropToSelectionClicked, this, [this]() {
        if (m_ctrl) m_ctrl->executeTool("crop_to_selection", {});
    });
    connect(m_toolBar, &ToolOptionsBar::cropAspectRatioChanged, this, [this](const QSizeF& r) {
        if (m_canvas) m_canvas->setCropAspectRatio(r);
    });
    connect(m_toolBar, &ToolOptionsBar::cropGuideChanged, this, [this](int t) {
        if (m_canvas) m_canvas->setCropGuideType(t);
    });
    connect(m_toolBar, &ToolOptionsBar::cropOverlayOpacityChanged, this, [this](float o) {
        if (m_canvas) m_canvas->setCropOverlayOpacity(o);
    });
    connect(m_toolBar, &ToolOptionsBar::cropStraightenChanged, this, [this](float a) {
        if (m_canvas) m_canvas->setCropStraightenAngle(a);
    });
    connect(m_toolBar, &ToolOptionsBar::cropCustomSizeChanged, this, [this](int w, int h) {
        if (m_canvas) m_canvas->setCropCustomSize(w, h);
    });
    connect(m_toolBar, &ToolOptionsBar::cropResetClicked, this, [this]() {
        if (m_canvas) { m_canvas->resetCrop(); m_canvas->update(); }
    });
    connect(m_toolBar, &ToolOptionsBar::cropCommitClicked, this, [this]() {
        if (m_canvas) m_canvas->commitCropAction();
    });
    connect(m_toolBar, &ToolOptionsBar::refineEdgeClicked, this, &MainWindow::openRefineEdgeDialog);
    connect(m_toolBar, &ToolOptionsBar::featherClicked, this, [this](int r) {
        applySelectRefinePreview(SelectRefineOp::Feather, r);
    });
    connect(m_toolBar, &ToolOptionsBar::growClicked, this, [this](int p) {
        applySelectRefinePreview(SelectRefineOp::Grow, p);
    });
    connect(m_toolBar, &ToolOptionsBar::shrinkClicked, this, [this](int p) {
        applySelectRefinePreview(SelectRefineOp::Shrink, p);
    });
    connect(m_toolBar, &ToolOptionsBar::borderClicked, this, [this](int p) {
        applySelectRefinePreview(SelectRefineOp::Border, p);
    });
    connect(m_toolBar, &ToolOptionsBar::smoothClicked, this, [this](int r) {
        applySelectRefinePreview(SelectRefineOp::Smooth, r);
    });

    auto* shortcuts = ShortcutManager::instance();
    shortcuts->registerCallback(QStringLiteral("select.all"), [this]() {
        if (m_ctrl) m_ctrl->executeTool("select_all", {});
    });
    shortcuts->registerCallback(QStringLiteral("select.deselect"), [this]() {
        if (m_ctrl) m_ctrl->executeTool("deselect", {});
    });
    shortcuts->registerCallback(QStringLiteral("select.invert"), [this]() {
        if (m_ctrl) m_ctrl->executeTool("select_invert", {});
    });
    shortcuts->registerCallback(QStringLiteral("select.refine"), [this]() {
        openRefineEdgeDialog();
    });
    shortcuts->registerCallback(QStringLiteral("selection.generative_fill"), [this]() {
        onGenerativeFill();
    });
    shortcuts->registerCallback(QStringLiteral("selection.crop_to_selection"), [this]() {
        if (m_ctrl) m_ctrl->executeTool("crop_to_selection", {});
    });
    // Channel edit target. The panel's row click and these go through the same
    // controller calls, so the keys and the mouse cannot drift apart; a plain
    // click/key selects AND shows that channel, which is why no visibility
    // handling lives here.
    shortcuts->registerCallback(QStringLiteral("channel.select.composite"), [this]() {
        if (m_ctrl) m_ctrl->selectCompositeChannel();
    });
    const char* const kComponentShortcutIds[3] = {"channel.select.red",
                                                  "channel.select.green",
                                                  "channel.select.blue"};
    for (int component = 0; component < 3; ++component) {
        shortcuts->registerCallback(QString::fromLatin1(kComponentShortcutIds[component]),
                                    [this, component]() {
            if (m_ctrl) m_ctrl->selectColorComponent(component, false);
        });
    }
    shortcuts->registerCallback(QStringLiteral("panel.brush_settings.toggle"), [this]() {
        showBrushSettingsPanel();
    });
    shortcuts->registerCallback(QStringLiteral("panel.layers.toggle"), [this]() {
        if (!m_layersDock || !m_layerPanel) return;
        if (m_layersDock->isVisible() && !m_layersDock->isHidden()) {
            m_layersDock->hide();
            return;
        }
        m_layersDock->show();
        m_layersDock->raise();
        m_layerPanel->focusLayerList();
    });
    shortcuts->registerCallback(QStringLiteral("panel.layers.focus"), [this]() {
        if (!m_layersDock || !m_layerPanel) return;
        m_layersDock->show();
        m_layersDock->raise();
        m_layerPanel->focusLayerList();
    });
    shortcuts->registerCallback(QStringLiteral("text.bold"), [this]() {
        if (!m_toolBar) return;
        const bool next = !m_toolBar->textBold();
        m_toolBar->setTextBold(next);
        onTextBoldChanged(next);
    });
    shortcuts->registerCallback(QStringLiteral("text.italic"), [this]() {
        if (!m_toolBar) return;
        const bool next = !m_toolBar->textItalic();
        m_toolBar->setTextItalic(next);
        onTextItalicChanged(next);
    });
    shortcuts->registerCallback(QStringLiteral("text.underline"), [this]() {
        if (!m_toolBar) return;
        const bool next = !m_toolBar->textUnderline();
        m_toolBar->setTextUnderline(next);
        onTextUnderlineChanged(next);
    });
    shortcuts->registerCallback(QStringLiteral("text.strikethrough"), [this]() {
        if (!m_toolBar) return;
        const bool next = !m_toolBar->textStrikethrough();
        m_toolBar->setTextStrikethrough(next);
        onTextStrikethroughChanged(next);
    });
    shortcuts->registerCallback(QStringLiteral("canvas.commit"), [this]() {
        if (!m_canvas) return;
        if (m_canvas->isDistortActive()) {
            m_canvas->commitDistort();
            updateDistortControls();
        } else if (m_canvas->isFreeTransformActive()) {
            m_canvas->commitFreeTransform();
        } else if (m_canvas->currentTool() == CanvasView::Tool::Crop && m_canvas->isCropActive()) {
            m_canvas->commitCropAction();
        }
    });
    shortcuts->registerCallback(QStringLiteral("canvas.cancel"), [this]() {
        if (!m_canvas) return;
        if (m_canvas->isDistortActive()) {
            m_canvas->cancelDistort();
            updateDistortControls();
        } else if (m_canvas->isFreeTransformActive()) {
            m_canvas->cancelFreeTransform();
        } else if (m_canvas->currentTool() == CanvasView::Tool::Crop && m_canvas->isCropActive()) {
            m_canvas->resetCrop();
            m_canvas->update();
        }
    });

    // ── Structural layer shortcuts ──
    // Group/Ungroup/Arrange/Clipping are owned by their Layer-menu QActions (see
    // above) to avoid double-firing. The actions below have no menu home (or no
    // default key) and need the dispatcher's context gating, so they stay as
    // ShortcutManager callbacks. All share the MainWindow::refreshLayersUi member.
    shortcuts->registerCallback(QStringLiteral("layer.new.quick"), [this]() {
        if (!m_ctrl || !m_doc) return;
        if (!m_doc->size.isValid() || m_doc->size.isNull())
            m_doc->size = QSize(1024, 768);
        m_ctrl->newLayer();
        refreshLayersUi();
    });
    shortcuts->registerCallback(QStringLiteral("layer.new.group"), [this]() { groupSelectedLayers(); });
    // Select Layer Above/Below walks the flat stack (index 0 = top). "Above"
    // moves toward the front (lower index); "Below" toward the back.
    auto selectIndex = [this](int idx) {
        if (!m_ctrl || !m_doc || idx < 0 || idx >= m_doc->flatCount()) return;
        m_ctrl->setActiveNode(idx);
        refreshLayersUi();
    };
    shortcuts->registerCallback(QStringLiteral("layer.select.above"), [this, selectIndex]() {
        if (m_ctrl) selectIndex(m_ctrl->activeLayerIndex() - 1);
    });
    shortcuts->registerCallback(QStringLiteral("layer.select.below"), [this, selectIndex]() {
        if (m_ctrl) selectIndex(m_ctrl->activeLayerIndex() + 1);
    });
    shortcuts->registerCallback(QStringLiteral("layer.select.top"), [selectIndex]() { selectIndex(0); });
    shortcuts->registerCallback(QStringLiteral("layer.select.bottom"), [this, selectIndex]() {
        if (m_doc) selectIndex(m_doc->flatCount() - 1);
    });
    shortcuts->registerCallback(QStringLiteral("layer.select.all"), [this]() {
        if (!m_ctrl) return;
        m_ctrl->selectAllNodes();
        refreshLayersUi();
    });
    // Lock toggles (Etapa 9, parcial): apply one LockFlag bit across the whole
    // multi-selection, deriving the new on/off state from the active node so a
    // mixed selection settles consistently (mirrors the panel's lock buttons).
    auto toggleLockBit = [this](int flagBit) {
        if (!m_ctrl || !m_doc) return;
        std::vector<int> indices(m_doc->selectedFlatIndices.begin(),
                                 m_doc->selectedFlatIndices.end());
        if (indices.empty() && m_doc->activeFlatIndex >= 0)
            indices.push_back(m_doc->activeFlatIndex);
        if (indices.empty()) return;
        const int refIdx = (m_doc->activeFlatIndex >= 0) ? m_doc->activeFlatIndex
                                                         : indices.front();
        auto* ref = m_doc->nodeAt(refIdx);
        const bool currentlyOn = ref && (ref->lockFlags & flagBit) != 0;
        m_ctrl->setNodesLockBit(indices, flagBit, !currentlyOn);
        refreshLayersUi();
    };
    shortcuts->registerCallback(QStringLiteral("layer.lock.transparent.toggle"), [toggleLockBit]() { toggleLockBit(LockTransparent); });
    shortcuts->registerCallback(QStringLiteral("layer.lock.image.toggle"),       [toggleLockBit]() { toggleLockBit(LockImage); });
    shortcuts->registerCallback(QStringLiteral("layer.lock.position.toggle"),    [toggleLockBit]() { toggleLockBit(LockPosition); });
    shortcuts->registerCallback(QStringLiteral("layer.lock.all.toggle"),         [toggleLockBit]() { toggleLockBit(LockAll); });

    // Layer-style clipboard (Etapa 10). No default keys — customizable, and also
    // surfaced in the Layer Panel context menu. The controller routes each
    // through commitLayerEffects (undo + Lock-All gating).
    shortcuts->registerCallback(QStringLiteral("layer.effects.copy"), [this]() {
        if (m_ctrl && m_doc) m_ctrl->copyLayerEffects(m_doc->activeFlatIndex);
    });
    shortcuts->registerCallback(QStringLiteral("layer.effects.paste"), [this]() {
        if (!m_ctrl) return;
        m_ctrl->pasteLayerEffects();
        refreshLayersUi();
    });
    shortcuts->registerCallback(QStringLiteral("layer.effects.clear"), [this]() {
        if (!m_ctrl || !m_doc) return;
        m_ctrl->clearLayerEffects(m_doc->activeFlatIndex);
        refreshLayersUi();
    });
    shortcuts->registerCallback(QStringLiteral("layer.effects.toggle_all"), [this]() {
        if (!m_ctrl || !m_doc) return;
        m_ctrl->toggleAllLayerEffects(m_doc->activeFlatIndex);
        refreshLayersUi();
    });

    // ── Etapa 2: keys migrated off CanvasView::keyPressEvent onto the dispatcher.
    // The qApp event filter resolves these before the canvas handler; their scope
    // (Canvas) suppresses them while a text field / canvas text edit is active,
    // so typing 'q', '\\' and Ctrl+I-as-Italic keep working.
    shortcuts->registerCallback(QStringLiteral("canvas.quick_mask"), [this]() {
        if (m_canvas) m_canvas->toggleQuickMask();
    });
    shortcuts->registerCallback(QStringLiteral("canvas.edit_mask"), [this]() {
        if (m_canvas) m_canvas->toggleMaskRubylith();
    });
    // Contextual Ctrl+I: invert the mask when it is the edit target, else invert
    // the layer pixels. (Italic in a text edit is handled by text.italic, which
    // outranks this Canvas-scope action while editing text.)
    shortcuts->registerCallback(QStringLiteral("edit.invert_contextual"), [this]() {
        if (!m_ctrl || !m_canvas || !m_doc) return;
        if (m_canvas->isEditingMask())
            m_ctrl->invertLayerMask(m_doc->activeFlatIndex);
        else
            m_ctrl->executeTool("invert_colors", {});
        m_canvas->update();
    });
    // Swap FG/BG colors (X) — Canvas scope; the Crop modal's X (Modal scope)
    // outranks it while cropping.
    shortcuts->registerCallback(QStringLiteral("color.swap"), [this]() {
        if (m_colorEngine) m_colorEngine->swapForegroundBackground();
    });
    // Crop modal keys. Modal scope also covers Free Transform / Distort, so guard
    // on the Crop tool actually being active before touching the crop controls.
    auto cropSessionActive = [this]() {
        return m_canvas && m_canvas->currentTool() == CanvasView::Tool::Crop
            && m_canvas->isCropActive();
    };
    shortcuts->registerCallback(QStringLiteral("tool.crop.cycle_guides"), [this, cropSessionActive]() {
        if (m_toolBar && cropSessionActive()) m_toolBar->cycleCropGuide();
    });
    shortcuts->registerCallback(QStringLiteral("tool.crop.swap_aspect_orientation"), [this, cropSessionActive]() {
        if (m_toolBar && cropSessionActive()) m_toolBar->swapCropAspect();
    });

    {
        auto* th0 = ThemeManager::instance()->current();
        qApp->setStyleSheet(th0->globalStyleSheet());
    }
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, [this, setupTabBg]() {
        auto* th = ThemeManager::instance()->current();
        qApp->setStyleSheet(th->globalStyleSheet());
        setupTabBg(m_tabWidget, th->colorPanelBackground);
        applyViewportStatusBarTheme();
    });

    // Home is the initial page, but currentChanged was connected only after
    // that page was inserted. Apply its temporary dock state once construction
    // has finished and every dock has been registered with QMainWindow.
    setHomeDockMode(true);
}

QList<QDockWidget*> MainWindow::panelDocks() const
{
    return { m_histogramDock, m_propsDock, m_colorDock, m_swatchesDock,
             m_historyDock, m_layersDock, m_channelsDock, m_agentDock,
             m_brushSettingsDock, m_brushPanelDock, m_customShapesPanelDock,
             m_generativeFillDock };
}

bool MainWindow::isDockTabbedInMainWindow(QDockWidget* dock) const
{
    // A hidden dock has no tab, and a floating one owns its window: both need
    // their title bar (it is the only handle Qt gives a floating panel here).
    if (!dock || dock->isHidden() || dock->isFloating())
        return false;

    // Docks grouped inside a floating QDockWidgetGroupWindow are reparented
    // onto that container and are no longer part of the main window's dock
    // area layout, so tabifiedDockWidgets() knows nothing about them. They keep
    // the title bar too: the group window is frameless on X11/Wayland, so
    // without it the whole group would become unmovable and unclosable.
    if (dock->parentWidget() != this)
        return false;

    // Qt lists tab mates that are currently closed as well; only a visible one
    // actually puts a second tab on screen.
    const QList<QDockWidget*> tabMates = tabifiedDockWidgets(dock);
    for (QDockWidget* mate : tabMates) {
        if (mate && !mate->isHidden())
            return true;
    }
    return false;
}

void MainWindow::updateDockTitleBar(QDockWidget* dock)
{
    if (!dock)
        return;

    const bool collapse = isDockTabbedInMainWindow(dock);
    QWidget* current = dock->titleBarWidget();
    if (collapse == (current != nullptr))
        return; // already in the wanted state

    if (collapse) {
        // An empty, zero-height title bar widget removes the bar entirely; the
        // tab above stays the drag handle for the panel.
        auto* collapsed = new QWidget(dock);
        collapsed->setFixedHeight(0);
        dock->setTitleBarWidget(collapsed);
    } else {
        dock->setTitleBarWidget(nullptr); // back to Qt's title bar, with title
        current->deleteLater();           // setTitleBarWidget() only detaches it
    }
}

void MainWindow::refreshDockTitleBars()
{
    for (QDockWidget* dock : panelDocks())
        updateDockTitleBar(dock);

    // This runs after every grouping change, which is exactly when Qt has
    // created or destroyed the tab bars the workspace autosave listens to.
    if (m_workspace)
        m_workspace->watchDockTabBars();
}

void MainWindow::scheduleRestoredDockChromeRefresh()
{
    // restoreState() recreates Qt's private dock tab bars/group windows,
    // but those new widgets do not reliably inherit the already-applied
    // application stylesheet. Re-polish them after the layout settles so
    // the native light frame/base never leaks around the restored column.
    QTimer::singleShot(0, this, [this]() {
        // restoreState() rebuilds the tab groups, so which docks deserve a
        // title bar changes with it.
        refreshDockTitleBars();

        for (QTabBar* tabBar : findChildren<QTabBar*>()) {
            tabBar->setDrawBase(false);
            tabBar->style()->unpolish(tabBar);
            tabBar->style()->polish(tabBar);
            tabBar->update();
        }
        for (QWidget* widget : findChildren<QWidget*>()) {
            if (qstrcmp(widget->metaObject()->className(),
                        "QDockWidgetGroupWindow") != 0)
                continue;
            widget->setAttribute(Qt::WA_StyledBackground, true);
            widget->style()->unpolish(widget);
            widget->style()->polish(widget);
            widget->update();
        }
    });
}

void MainWindow::scheduleDockTitleBarRefresh()
{
    if (m_dockTitleBarRefreshScheduled)
        return;
    m_dockTitleBarRefreshScheduled = true;

    // Swapping a title bar widget re-runs the dock layout, so stay out of Qt's
    // way while a drag is still in flight (mouse button held) and retry after.
    const int delayMs = (QApplication::mouseButtons() != Qt::NoButton) ? 100 : 0;
    QTimer::singleShot(delayMs, this, [this]() {
        m_dockTitleBarRefreshScheduled = false;
        if (QApplication::mouseButtons() != Qt::NoButton) {
            scheduleDockTitleBarRefresh();
            return;
        }
        refreshDockTitleBars();
    });
}

void MainWindow::watchDockTitleBar(QDockWidget* dock)
{
    if (!dock)
        return;

    // Every one of these means the tab grouping may have changed — and not only
    // for this dock: when a tab is dragged out of a group, the docks left behind
    // emit nothing at all, so each signal refreshes the whole set.
    connect(dock, &QDockWidget::dockLocationChanged, this,
            [this](Qt::DockWidgetArea) { scheduleDockTitleBarRefresh(); });
    connect(dock, &QDockWidget::topLevelChanged, this,
            [this](bool) { scheduleDockTitleBarRefresh(); });
    connect(dock, &QDockWidget::visibilityChanged, this,
            [this](bool) { scheduleDockTitleBarRefresh(); });
}

void MainWindow::applyViewportStatusBarTheme()
{
    if (!m_viewportStatusBar)
        return;

    auto* th = ThemeManager::instance()->current();
    m_viewportStatusBar->setStyleSheet(QStringLiteral(
        "QWidget#ViewportStatusBar {"
        " background: %1;"
        " border-top: %2px solid %3;"
        "}"
        "QWidget#ViewportStatusBar QLabel {"
        " color: %4;"
        "}"
    ).arg(th->colorSurface.name())
     .arg(th->borderThinWidth)
     .arg(th->colorBorder.name())
     .arg(th->colorTextPrimary.name()));

    if (auto* barLayout = m_viewportStatusBar->layout()) {
        barLayout->setContentsMargins(
            th->spaceMD, th->spaceXS,
            th->spaceMD, th->spaceXS);
        barLayout->setSpacing(th->spaceLG);
    }
}

void MainWindow::showViewportStatusMessage(const QString& message, int timeoutMs,
                                           const QString& iconPath)
{
    if (!m_statusMessageLabel)
        return;

    m_statusMessageLabel->setText(message.isEmpty() ? tr("Ready") : message);
    if (m_statusIconLabel) {
        if (!iconPath.isEmpty()) {
            m_statusIconLabel->setPixmap(QPixmap(iconPath));
            m_statusIconLabel->setVisible(true);
        } else {
            m_statusIconLabel->clear();
            m_statusIconLabel->setVisible(false);
        }
    }
    if (m_statusMessageTimer) {
        m_statusMessageTimer->stop();
        if (timeoutMs > 0)
            m_statusMessageTimer->start(timeoutMs);
    }
}

MainWindow::DroppedImageAction MainWindow::askDroppedImageAction()
{
    const QList<AppMessageDialog::ButtonSpec> buttons = {
        { tr("Cancel"), QMessageBox::RejectRole },
        { tr("Insert into Canvas"), QMessageBox::ActionRole },
        { tr("Open Image"), QMessageBox::AcceptRole },
    };
    const int clicked = AppMessageDialog::choice(
        this, tr("Open Image"), tr("How do you want to use this image?"),
        AppMessageDialog::Icon::Question, buttons, /*defaultIndex=*/1);
    if (clicked == 1) return DroppedImageAction::InsertIntoCanvas;
    if (clicked == 2) return DroppedImageAction::OpenAsNewDocument;
    return DroppedImageAction::Cancel;
}

bool MainWindow::openDroppedImagesAsNewDocument(const QStringList& paths)
{
    if (paths.isEmpty()) return false;

    // A dropped .hazor (or legacy .hzs) opens through the same project path
    // as File > Open — there is no "insert into canvas" or flatten-on-import
    // interpretation for a whole project, so it is dispatched before anything
    // else below.
    if (paths.front().endsWith(QStringLiteral(".hazor"), Qt::CaseInsensitive)
        || paths.front().endsWith(QStringLiteral(".hzs"), Qt::CaseInsensitive)) {
        if (!loadProjectAsNewTab(paths.front())) {
            AppMessageDialog::warning(this, tr("Open Project"), tr("Failed to open project."));
            return false;
        }
        return true;
    }

    // A RAW file gets its own non-destructive RAW Layer and opens straight
    // into Hazor Develop, same as File > Open — not the generic
    // flatten-on-import codec path below.
    if (RawEngine::isSupportedExtension(
            normalizeImageExtension(QFileInfo(paths.front()).suffix()).toStdString())) {
        openRawFileAsNewTab(paths.front());
        return true;
    }

    const ImageLoadResult loaded = imageCodecRegistry().readImage(paths.front());
    std::optional<ColorManagedOpenResult> colorManaged;
    if (loaded.ok) {
        colorManaged = prepareImageForOpenWithDialogs(
            this,
            loaded.image,
            ColorManagementService::instance().defaultOpenContext(paths.front()));
        if (!colorManaged)
            return false;
    }
    const QImage probe = colorManaged
        ? convertDocumentImageToQImage(colorManaged->image)
        : QImage();
    const QSize s = probe.isNull() ? QSize(1024, 768) : probe.size();

    const QString title = QFileInfo(paths.front()).fileName();
    auto& tab = createTab(title, s);
    tab.isDirty = false;
    tab.currentProjectPath.clear();
    if (m_doc) {
        m_doc->size = s;
        m_doc->selection.resize(s.width(), s.height());
        m_doc->selection.clear();
        m_doc->selection.setActive(false);
        if (colorManaged && colorManaged->finalDocumentProfile.isValid()) {
            m_doc->setColorProfile(colorManaged->finalDocumentProfile);
            m_doc->setProfileSource(colorManaged->finalDocumentProfile.source());
        }
    }

    // New, empty document: there is no prior pan/zoom state to place the drop
    // relative to, so every imported path lands centered (dropCanvasNdc = origin).
    QList<int> rawLayerFlatIndices;
    if (m_ctrl && !m_ctrl->importExternalImages(paths, QPointF(0.0, 0.0), &rawLayerFlatIndices)) {
        AppMessageDialog::warning(this, tr("Import Image"),
            tr("No supported image could be imported from the dropped files."));
    }

    refreshLayerPanel();
    refreshPropertiesPanel();
    updateLayerMenuState();
    updateStatusFromDocument();
    if (colorManaged && !colorManaged->warningMessage.isEmpty())
        showViewportStatusMessage(colorManaged->warningMessage, 7000);
    for (int flatIndex : rawLayerFlatIndices)
        openHazorDevelop(flatIndex);
    return true;
}

MainWindow::DocumentTab& MainWindow::createTab(const QString& name, const QSize& size)
{
    auto doc = std::make_shared<Document>();
    doc->name = name;
    doc->size = size;
    doc->zoom = 1.0f;

    // Every tab is backed by a container, even a brand-new untitled document
    // with no Smart Object in it: that way "save the container" is the only
    // save path there is, and adding the first Smart Object later needs no
    // migration (see DocumentContainer.hpp).
    auto container = std::make_shared<DocumentContainer>(doc);
    return createTabForDocument(name, container, container->rootId());
}

MainWindow::DocumentTab& MainWindow::createTabForDocument(
    const QString& name,
    const std::shared_ptr<DocumentContainer>& container,
    const SmartObjectDocId& docId)
{
    // A tab editing a Smart Object's contents gets the container's open EDIT
    // SESSION — the isolated working copy — and not the stored document: its
    // edits must stay out of the project until Ctrl+S commits them. Every other
    // tab (the root, a linked project's own tab) borrows the stored document
    // itself, so an edit there is an edit in the container.
    std::shared_ptr<Document> doc;
    if (container) {
        doc = container->editSession(docId);
        if (!doc)
            doc = container->document(docId);
    }
    Q_ASSERT(doc);

    auto ctrl = std::make_unique<ImageController>();
    auto* ctrlPtr = ctrl.get();

    auto* page = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    auto* canvas = new CanvasView(doc.get(), ctrlPtr, page);
    if (m_toolBar)
        canvas->setGradientDefinition(m_toolBar->gradientDefinition());
    pageLayout->addWidget(canvas);
    canvas->reloadRulerGuideSettings();
    ctrlPtr->setDocument(doc.get());
    // The controller needs the container to resolve a Smart Object's docId.
    // Shared, never owned: the registry is the only owner.
    ctrlPtr->setDocumentContainer(container, docId);

    // Editing ANY document of the container can invalidate Smart Object
    // instances of it — including in other documents of the same file. The
    // refresh is debounced and always driven by the container's root
    // controller, so one cascade owner exists per file no matter how many tabs
    // are open on it.
    connect(ctrlPtr, &ImageController::imageChanged,
            this, &MainWindow::scheduleSmartObjectRefresh);
    connect(ctrlPtr, &ImageController::documentChanged,
            this, &MainWindow::scheduleSmartObjectRefresh);
    connect(ctrlPtr, &ImageController::smartObjectRenderCommitted, this,
            [this, ctrlPtr](const SmartObjectDocId&, const QStringList& affected,
                            bool marksDirty) {
        onSmartObjectRenderCommitted(ctrlPtr->documentContainerPtr(), affected, marksDirty);
    });
    connect(ctrlPtr, &ImageController::smartObjectLinkStateChanged, this,
            [this](int, bool) { refreshLayerPanel(); });

    // Adjustment editors bracket a live slider/curve drag through the controller
    // so the canvas previews on the GPU per-layer compositor instead of
    // recompositing the whole CPU projection every frame (slow on large docs).
    connect(ctrlPtr, &ImageController::adjustmentLiveEditChanged,
            canvas, &CanvasView::setAdjustmentLiveEdit);

    connect(ctrlPtr, &ImageController::cloneSourceRequested, canvas, &CanvasView::setCloneSource);
    connect(ctrlPtr, &ImageController::cloneSampleModeRequested, canvas,
            qOverload<int>(&CanvasView::setCloneSampleMode));
    connect(ctrlPtr, &ImageController::cloneAlignedRequested, canvas, &CanvasView::setCloneAligned);
    connect(ctrlPtr, &ImageController::cloneStrokeBeginRequested, canvas, &CanvasView::beginCloneStroke);
    connect(ctrlPtr, &ImageController::cloneStrokeUpdateRequested, canvas, &CanvasView::updateCloneStroke);
    connect(ctrlPtr, &ImageController::cloneStrokeEndRequested, canvas, &CanvasView::endCloneStroke);

    connect(ctrlPtr, &ImageController::healingSourceRequested, canvas, &CanvasView::setHealingSource);
    connect(ctrlPtr, &ImageController::healingSampleModeRequested, canvas,
            qOverload<int>(&CanvasView::setCloneSampleMode));
    connect(ctrlPtr, &ImageController::healingAlignedRequested, canvas, &CanvasView::setCloneAligned);
    connect(ctrlPtr, &ImageController::healingDiffusionRequested, canvas, &CanvasView::setHealingDiffusion);
    connect(ctrlPtr, &ImageController::healingStrokeBeginRequested, canvas, &CanvasView::beginHealingStroke);
    connect(ctrlPtr, &ImageController::healingStrokeUpdateRequested, canvas, &CanvasView::updateHealingStroke);
    connect(ctrlPtr, &ImageController::healingStrokeEndRequested, canvas, &CanvasView::endHealingStroke);

    connect(canvas, &CanvasView::toolChanged, this, [this, canvas](int tool) {
        if (tool == static_cast<int>(CanvasView::Tool::AiRemove)) {
            // Temporarily hidden: AI Remove Tool will be released later.
            // Do not remove the implementation. Only the user-facing UI is disabled.
            canvas->setTool(CanvasView::Tool::Move);
            return;
        }

        if (m_toolsPanel)
            m_toolsPanel->setActiveTool(tool);

        // When Move tool is active, the options bar page is driven by the active
        // layer type. For all other tools (including Skew), show their own page.
        if (tool != static_cast<int>(CanvasView::Tool::Move))
            m_toolBar->setTool(tool);
        else
            applyMoveOptionsPage();
        if (tool == static_cast<int>(CanvasView::Tool::Select) && m_toolsPanel) {
            const int activeSubTool = m_toolsPanel->activeSubToolForTool(tool);
            m_toolBar->setSubToolsForTool(tool, m_toolsPanel->activeSubToolsForTool(tool), activeSubTool);
            canvas->setSelectType(static_cast<SelectType>(activeSubTool));
        }
        // Clone Stamp: keep the flyout button + options page in sync with the
        // active stamp sub-mode (the canvas is the source of truth, e.g. when the
        // agent switches to healing mode).
        if (tool == static_cast<int>(CanvasView::Tool::CloneStamp)) {
            const int mode = canvas->isHealingMode() ? 1 : 0;
            if (m_toolsPanel)
                m_toolsPanel->setActiveSubTool(static_cast<int>(CanvasView::Tool::CloneStamp), mode);
            m_toolBar->setStampMode(mode);
        }
        if (tool == static_cast<int>(CanvasView::Tool::AiSelect)) {
            m_toolBar->setAiToolPageMode(AiToolPageMode::ObjectSelection);
            m_toolBar->bindAiController(canvas->aiSelectionController());
            ensureAiControllerConnected();
        }
        // Temporarily hidden: AI Remove Tool will be released later.
        // Do not remove the implementation. Only the user-facing UI is disabled.
        /*
        else if (tool == static_cast<int>(CanvasView::Tool::AiRemove)) {
            m_toolBar->setAiToolPageMode(AiToolPageMode::RemoveObject);
            m_toolBar->bindAiRemoveController(canvas->aiRemoveController());
        }
        */
        updateToolBarLayout();
        // Show/hide the transform bounding box based on the active tool:
        // restore the toolbar checkbox state when entering Move, clear it otherwise.
        if (m_canvas) {
            if (tool == static_cast<int>(CanvasView::Tool::Move))
                m_canvas->setShowTransformControls(m_toolBar->showTransformControls());
            else
                m_canvas->setShowTransformControls(false);
        }
        updateDistortControls();
        // The Align auxiliary page is gated on the active tool — re-evaluate it.
        updateAlignBarState();
    });
    connect(canvas, &CanvasView::freeTransformStateChanged, this, [this](bool active) {
        if (!m_toolBar || !m_canvas)
            return;
        if (active) {
            refreshTransformOptionsBar();
            m_toolBar->showTransformOptions(true);
        } else if (m_canvas->currentTool() == CanvasView::Tool::Move) {
            applyMoveOptionsPage();
        } else {
            m_toolBar->setTool(static_cast<int>(m_canvas->currentTool()));
        }
        updateToolBarLayout();
    });
    connect(canvas, &CanvasView::activeTransformChanged, this, [this]() {
        refreshTransformOptionsBar();
        refreshPropertiesPanel();
    });
    connect(canvas, &CanvasView::selectTypeChanged, this, [this](int type) {
        const int selectTool = static_cast<int>(CanvasView::Tool::Select);
        if (m_toolsPanel)
            m_toolsPanel->setActiveSubTool(selectTool, type);
        if (m_toolBar) {
            if (m_toolsPanel) {
                m_toolBar->setSubToolsForTool(selectTool,
                    m_toolsPanel->activeSubToolsForTool(selectTool), type);
            } else {
                m_toolBar->setActiveSubTool(selectTool, type);
            }
        }
    });
    connect(canvas, &CanvasView::distortStateChanged, this, [this]() {
        updateDistortControls();
    });
    connect(canvas, &CanvasView::textStyleContextChanged, this, [this, canvas]() {
        if (!m_toolBar) return;
        TextSpan cs;
        ParagraphStyle ps;
        if (!canvas->currentTextContext(cs, ps)) return;
        QFont f(cs.fontFamily);
        f.setPixelSize(std::max(1, static_cast<int>(std::round(cs.fontSize))));
        f.setBold(cs.bold);
        f.setItalic(cs.italic);
        m_toolBar->setTextFont(f);
        m_toolBar->setTextSize(static_cast<int>(std::round(cs.fontSize)));
        m_toolBar->setTextBold(cs.bold);
        m_toolBar->setTextItalic(cs.italic);
        m_toolBar->setTextUnderline(cs.underline);
        m_toolBar->setTextStrikethrough(cs.strikethrough);
        m_toolBar->setTextColor(cs.color);
        m_toolBar->setTextTracking(cs.letterSpacing);
        m_toolBar->setTextAlign(static_cast<int>(ps.alignment));
        m_toolBar->setTextLeading(ps.lineHeight);
    });
    connect(canvas, &CanvasView::zoomChanged, this, [this, canvas](float zoom) {
        if (canvas == m_canvas)
            onZoomChanged(zoom);
        updateDirtyUi();
    });
    // The Channels panel builds its RGB / R-G-B thumbnails from this canvas'
    // CPU projection, which is composited off thread and therefore lands after
    // the imageChanged() that triggered it. Without this the thumbnails stayed
    // one edit behind until the next composition change. Only the active tab's
    // canvas feeds the panel.
    connect(canvas, &CanvasView::projectionUpdated, this, [this, canvas]() {
        if (canvas == m_canvas && m_channelsPanel)
            m_channelsPanel->refresh();
    });
    connect(canvas, &CanvasView::brushSizeChanged, this, [this](float size) {
        if (m_toolBar)
            m_toolBar->setBrushSize(qRound(size));
        if (m_brushPanel)
            m_brushPanel->setSize(size);
    });
    connect(canvas, &CanvasView::brushHardnessChanged, this, [this](float hardness) {
        if (m_toolBar)
            m_toolBar->setBrushHardness(qRound(hardness * 100.0f));
        if (m_brushPanel)
            m_brushPanel->setHardness(hardness);
    });
    connect(canvas, &CanvasView::mouseImageCoordChanged, this, &MainWindow::onMouseCoordChanged);
    connect(canvas, &CanvasView::undoRequested, this, &MainWindow::onUndo);
    connect(canvas, &CanvasView::redoRequested, this, &MainWindow::onRedo);
    connect(canvas, &CanvasView::contextMenuRequested, this, &MainWindow::onCanvasContextMenuRequested);
    connect(canvas, &CanvasView::colorSampled, this, [this](const QColor& color) {
        if (!m_colorEngine) return;
        m_colorEngine->setForegroundColor(color);
    });
    connect(canvas, &CanvasView::backgroundColorSampled, this, [this](const QColor& color) {
        if (!m_colorEngine) return;
        m_colorEngine->setBackgroundColor(color);
    });
    // Curves editor eyedropper/target: route the click position to the editor,
    // which samples its own bypassed input composite (no self-accumulation).
    connect(canvas, &CanvasView::curvesPickRequested, this,
            [this](const QPointF& docPos, int mode) {
        if (m_propsPanel && m_propsPanel->curvesEditor())
            m_propsPanel->curvesEditor()->pickInputColor(docPos, mode);
    });
    connect(canvas, &CanvasView::curvesTargetBegan, this, [this](const QPointF& docPos) {
        if (m_propsPanel && m_propsPanel->curvesEditor())
            m_propsPanel->curvesEditor()->targetBegan(docPos);
    });
    connect(canvas, &CanvasView::curvesTargetDragged, this, [this](int dy) {
        if (m_propsPanel && m_propsPanel->curvesEditor())
            m_propsPanel->curvesEditor()->targetDragged(dy);
    });
    connect(canvas, &CanvasView::curvesTargetEnded, this, [this]() {
        if (m_propsPanel && m_propsPanel->curvesEditor())
            m_propsPanel->curvesEditor()->targetEnded();
    });
    // Hue/Saturation editor eyedropper: route canvas picks to the editor, which
    // samples its own bypassed input composite (main = one click; add/subtract
    // are press-drag-release gestures committed as a single undo step).
    connect(canvas, &CanvasView::hueSatPickClicked, this,
            [this](const QPointF& docPos, int mode) {
        if (m_propsPanel && m_propsPanel->hueSaturationEditor())
            m_propsPanel->hueSaturationEditor()->onPickClicked(docPos, mode);
    });
    connect(canvas, &CanvasView::hueSatPickDragBegan, this,
            [this](const QPointF& docPos, int mode) {
        if (m_propsPanel && m_propsPanel->hueSaturationEditor())
            m_propsPanel->hueSaturationEditor()->onPickDragBegan(docPos, mode);
    });
    connect(canvas, &CanvasView::hueSatPickDragMoved, this, [this](const QPointF& docPos) {
        if (m_propsPanel && m_propsPanel->hueSaturationEditor())
            m_propsPanel->hueSaturationEditor()->onPickDragMoved(docPos);
    });
    connect(canvas, &CanvasView::hueSatPickDragEnded, this, [this]() {
        if (m_propsPanel && m_propsPanel->hueSaturationEditor())
            m_propsPanel->hueSaturationEditor()->onPickDragEnded();
    });
    connect(canvas, &CanvasView::hueSatPickCancelled, this, [this]() {
        if (m_propsPanel && m_propsPanel->hueSaturationEditor())
            m_propsPanel->hueSaturationEditor()->onPickCancelled();
    });
    connect(canvas, &CanvasView::externalImagesDropped, this, [this, canvas](const QStringList& paths, QPointF screenPos) {
        if (!canvas || paths.isEmpty()) return;

        // A project file always opens as its own document — skip the
        // Insert-into-Canvas/Open-Image question below, which only makes
        // sense for a plain image.
        if (paths.front().endsWith(QStringLiteral(".hazor"), Qt::CaseInsensitive)
            || paths.front().endsWith(QStringLiteral(".hzs"), Qt::CaseInsensitive)) {
            openDroppedImagesAsNewDocument(paths);
            return;
        }

        // No document open: nothing to offer a choice for, open directly.
        if (!m_ctrl || !m_doc) {
            openDroppedImagesAsNewDocument(paths);
            return;
        }

        const DroppedImageAction action = askDroppedImageAction();
        if (action == DroppedImageAction::Cancel) return;
        if (action == DroppedImageAction::OpenAsNewDocument) {
            openDroppedImagesAsNewDocument(paths);
            return;
        }

        const QPointF ndc = canvas->screenToCanvasNdc(screenPos);
        QList<int> rawLayerFlatIndices;
        if (!m_ctrl->importExternalImages(paths, ndc, &rawLayerFlatIndices)) {
            AppMessageDialog::warning(this, tr("Import Image"),
                tr("No supported image could be imported from the dropped files."));
            return;
        }
        refreshLayerPanel();
        refreshPropertiesPanel();
        updateLayerMenuState();
        for (int flatIndex : rawLayerFlatIndices)
            openHazorDevelop(flatIndex);
    });

    connect(ctrlPtr, &ImageController::imageChanged, canvas, [canvas]() {
        canvas->syncLayersToGpu();
        canvas->markSelectMaskDirty();
        canvas->update();
    });
    connect(ctrlPtr, &ImageController::layerChanged, this, [this](int flatIndex) {
        refreshLayerPanel();
        refreshPropertiesPanel();
        // Lock/property changes arrive as layerChanged (not activeLayerChanged),
        // so the lock-aware menu enable states must refresh here too — otherwise
        // they only update on the next layer (re)selection.
        updateLayerMenuState();
        // A layer's type/lock can change in place (e.g. rasterize, position lock);
        // the Align page's compatibility rule must re-evaluate on those too.
        updateAlignBarState();
        markActiveTabDirty(true);
        if (m_doc && m_toolBar) {
            // Prefer the layer that actually changed: when a Text layer is baked
            // inside a transformed group, the active node is the GROUP, so reading
            // it would miss the child's recalculated font size. Fall back to the
            // active node for in-place edits that don't carry an index.
            auto* node = m_doc->nodeAt(flatIndex);
            if (!node || !node->layer || !node->layer->textData)
                node = m_doc->activeNode();
            if (node && node->layer && node->layer->textData) {
                auto& td = *node->layer->textData;
                if (!td.spans.empty()) {
                    auto& s = td.spans.back();
                    m_toolBar->setTextSize(static_cast<int>(std::round(s.fontSize)));
                    m_toolBar->setTextColor(s.color);
                }
            }
        }
        updateDirtyUi();
    });
    connect(ctrlPtr, &ImageController::activeLayerChanged, this, [this](int idx) {
        Q_UNUSED(idx)
        refreshLayerPanel();
        refreshPropertiesPanel();
        updateLayerMenuState();
        updateAlignBarState();
        // Creating or selecting an Adjustment Layer surfaces its editor: bring the
        // Properties dock to the front of its tab stack so the controls are
        // immediately reachable.
        if (m_doc && m_propsDock) {
            auto* active = m_doc->activeNode();
            if (active && active->isAdjustmentLayer()) {
                m_propsDock->show();
                m_propsDock->raise();
            }
        }
        // While Move tool is active, show the options bar that matches the
        // selected layer type so the user can edit Text/Shape properties
        // without switching tools.
        if (m_canvas && m_canvas->currentTool() == CanvasView::Tool::Move) {
            applyMoveOptionsPage();
            updateToolBarLayout();
        }
        updateDistortControls();
        if (m_canvas) m_canvas->update();
        updateDirtyUi();
    });

    // Multi-selection changes (e.g. box-select on canvas) don't necessarily fire
    // activeLayerChanged, so refresh the Align bar on selection changes too.
    connect(ctrlPtr, &ImageController::selectionChanged, this, [this]() {
        updateAlignBarState();
        // Selection-dependent menu items (e.g. Layer Mask > From Selection) gate on
        // selection.active(); without this they'd only refresh on the next layer
        // (re)selection — so creating a selection on a freshly pasted layer left
        // "From Selection" disabled until the user clicked another layer.
        updateLayerMenuState();
        // The selection changed through some other path: the cached refine
        // base no longer matches it (re-clicking Feather would resurrect the
        // old selection). Skip while the refine itself is mutating.
        if (!m_inSelectRefine)
            clearSelectRefinePreview();
    });

    connect(ctrlPtr, &ImageController::historyChanged, this, [this]() {
        refreshHistoryPanel();
        if (m_canvas) m_canvas->update();
        markActiveTabDirty(true);
    });

    connect(ctrlPtr, &ImageController::guidesChanged, this, [this]() {
        if (m_canvas) m_canvas->update();
        markActiveTabDirty(true);
    });

    connect(ctrlPtr, &ImageController::documentChanged, this, [this]() {
        updateStatusFromDocument();
        updateLayerMenuState();
        // Canvas resize / resolution / undo-redo arrive here; keep the Document
        // page's W/H/Resolution fields in sync with the active document.
        refreshPropertiesPanel();
        markActiveTabDirty(true);
        updateDirtyUi();
    });

    connect(ctrlPtr, &ImageController::maskEditingChanged, this, [this](bool editing) {
        Q_UNUSED(editing)
        refreshPropertiesPanel();
        updateLayerMenuState();
        updateDirtyUi();
    });

    // Keep the Properties panel's Show Overlay checkbox in sync when the
    // overlay is toggled elsewhere (e.g. the "\" shortcut on the canvas).
    connect(ctrlPtr, &ImageController::maskOverlayChanged, this, [this]() {
        refreshPropertiesPanel();
    });

    connect(ctrlPtr, &ImageController::operationBlocked, this, [this](const QString& msg) {
        showViewportStatusMessage(msg, 3000, QStringLiteral(":/icons/status-lock.png"));
    });

    connect(ctrlPtr, &ImageController::progressOperationStarted, this,
            [this, ctrlPtr](uint64_t jobId, const QString& message, bool cancelable) {
        m_activeProgressJobId = jobId;
        m_progressController = ctrlPtr;
        beginProgressDelayed(tr("Progress"), message, cancelable);
    });
    connect(ctrlPtr, &ImageController::progressOperationMessageChanged, this,
            [this](uint64_t jobId, const QString& message) {
        if (jobId == m_activeProgressJobId)
            setMessage(message);
    });
    connect(ctrlPtr, &ImageController::progressOperationProgressChanged, this,
            [this](uint64_t jobId, int progress) {
        if (jobId == m_activeProgressJobId)
            setProgress(progress);
    });
    connect(ctrlPtr, &ImageController::progressOperationFinished, this,
            [this](uint64_t jobId) {
        if (jobId == m_activeProgressJobId)
            closeProgress();
    });
    connect(ctrlPtr, &ImageController::progressOperationCanceled, this,
            [this](uint64_t jobId) {
        if (jobId == m_activeProgressJobId) {
            closeProgress();
            showViewportStatusMessage(tr("Operation canceled."), 2500);
        }
    });
    connect(ctrlPtr, &ImageController::progressOperationFailed, this,
            [this](uint64_t jobId, const QString& error) {
        if (jobId == m_activeProgressJobId) {
            closeProgress();
            AppMessageDialog::warning(this, tr("Progress"), error);
        }
    });
    connect(ctrlPtr, &ImageController::upscaleNewDocumentReady, this,
            [this](const QImage& image, const QString& name,
                   const ColorProfile& profile, ColorProfileSource source) {
        if (image.isNull())
            return;
        auto& tab = createTab(name, image.size());
        tab.document->setColorProfile(profile);
        tab.document->setProfileSource(source);
        tab.controller->importImage(image, name);
        if (tab.canvas) {
            tab.canvas->syncLayersToGpu();
            tab.canvas->update();
        }
        refreshLayerPanel();
        refreshPropertiesPanel();
        updateStatusFromDocument();
    });

    // Window-scope delete handler via event filter (catches Delete even when LayerPanel has focus)
    qApp->installEventFilter(this);

    const int tabWidgetIndex = m_tabWidget->addTab(page, name);
    m_tabWidget->setTabToolTip(tabWidgetIndex, name);

    DocumentTab tab;
    tab.name = name;
    tab.currentProjectPath = container ? container->path() : QString();
    tab.isDirty = false;
    tab.container = container;
    tab.document = std::move(doc);
    tab.docId = docId;
    tab.page = page;
    tab.canvas = canvas;
    tab.controller = std::move(ctrl);
    m_tabs.push_back(std::move(tab));
    m_activeTabIndex = static_cast<int>(m_tabs.size()) - 1;
    m_doc = m_tabs.back().document.get();
    m_canvas = m_tabs.back().canvas;
    m_ctrl = m_tabs.back().controller.get();

    // Selecting the page emits currentChanged synchronously. Register its
    // DocumentTab first so onTabChanged never observes the new canvas as a
    // transient "no document" state and clears the surrounding workspace.
    m_tabWidget->setCurrentIndex(tabWidgetIndex);

    m_toolExecutor->setController(m_ctrl);
    m_layerPanel->setController(m_ctrl);
    syncChannelsPanel();
    m_propsPanel->setController(m_ctrl);
    m_histogramPanel->setController(m_ctrl);
    syncGenerativeFillPanelController();

    if (m_presetManager && !m_presetManager->presetNames(Assistant).isEmpty())
        m_agentController->applyConfig(m_presetManager->activeAssistant());

    if (m_canvas->currentTool() == CanvasView::Tool::Move)
        applyMoveOptionsPage();
    else
        m_toolBar->setTool(static_cast<int>(m_canvas->currentTool()));
    updateToolBarLayout();
    updateAlignBarState();
    updateDistortControls();
    if (m_colorEngine) {
        m_canvas->setBrushColor(m_colorEngine->foregroundColor());
        m_canvas->setFillBucketColor(m_colorEngine->foregroundColor());
        m_canvas->setShapeFillColor(m_colorEngine->foregroundColor());
    }
    refreshHistoryPanel();
    return m_tabs.back();
}

void MainWindow::reloadRulerGuideSettingsForTabs()
{
    for (auto& tab : m_tabs) {
        if (tab.canvas) {
            tab.canvas->reloadRulerGuideSettings();
            tab.canvas->update();
        }
    }
}

void MainWindow::applyRulerGuideSetting(
    const std::function<void(RulerGuideSettings&)>& mutate)
{
    RulerGuideSettings settings = RulerGuideSettings::load();
    mutate(settings);
    RulerGuideSettings::save(settings);
    reloadRulerGuideSettingsForTabs();
    updateRulerGuideActionStates();
    // Keep the Properties panel's Rulers & Grids controls in sync.
    if (m_propsPanel)
        m_propsPanel->setRulerGuideState(settings.rulers.showRulers,
                                         static_cast<int>(settings.rulers.unit));
}

void MainWindow::updateRulerGuideActionStates()
{
    const RulerGuideSettings settings = RulerGuideSettings::load();
    auto setCheckedQuietly = [](QAction* action, bool checked) {
        if (!action)
            return;
        QSignalBlocker blocker(action);
        action->setChecked(checked);
    };

    setCheckedQuietly(m_showRulersAction, settings.rulers.showRulers);
    setCheckedQuietly(m_showGuidesAction, settings.guides.showGuides);
    setCheckedQuietly(m_lockGuidesAction, settings.guides.lockGuides);
    setCheckedQuietly(m_enableSnapAction, settings.snap.enabled);
    setCheckedQuietly(m_snapToCanvasAction,
                      settings.snap.snapToCanvasBounds && settings.snap.snapToCanvasCenter);
    setCheckedQuietly(m_snapToGuidesAction,
                      settings.snap.snapToGuides && settings.guides.snapToGuides);

    if (m_showColorPaletteAction) {
        QSettings s;
        const bool visible = s.value(QStringLiteral("View/colorPaletteVisible"), false).toBool();
        setCheckedQuietly(m_showColorPaletteAction, visible);
        if (m_colorPaletteBar)
            m_colorPaletteBar->setVisible(visible);
    }
}

void MainWindow::updateToolBarLayout()
{
    if (!m_toolBar)
        return;

    m_toolBar->updateGeometry();
}

CanvasView* MainWindow::activeCanvas() const
{
    return m_canvas;
}

Document* MainWindow::activeDocument() const
{
    return m_doc;
}

ImageController* MainWindow::activeController() const
{
    return m_ctrl;
}

void MainWindow::syncGenerativeFillPanelController()
{
    if (m_generativeFillPanel)
        m_generativeFillPanel->setController(m_ctrl);
}

int MainWindow::tabWidgetIndexForDocument(int documentIndex) const
{
    return documentIndex < 0 ? -1 : documentIndex + 1;
}

int MainWindow::documentIndexForTabWidget(int tabWidgetIndex) const
{
    return tabWidgetIndex <= 0 ? -1 : tabWidgetIndex - 1;
}

void MainWindow::syncDocumentTabOrder()
{
    std::vector<DocumentTab> ordered;
    ordered.reserve(m_tabs.size());
    std::vector<bool> moved(m_tabs.size(), false);

    for (int tabWidgetIndex = 1; tabWidgetIndex < m_tabWidget->count(); ++tabWidgetIndex) {
        QWidget* page = m_tabWidget->widget(tabWidgetIndex);
        for (int documentIndex = 0; documentIndex < static_cast<int>(m_tabs.size()); ++documentIndex) {
            if (!moved[documentIndex] && m_tabs[documentIndex].page == page) {
                ordered.push_back(std::move(m_tabs[documentIndex]));
                moved[documentIndex] = true;
                break;
            }
        }
    }
    if (ordered.size() == m_tabs.size())
        m_tabs = std::move(ordered);
}

void MainWindow::onTabMoved(int from, int to)
{
    Q_UNUSED(from)
    Q_UNUSED(to)
    if (!m_tabWidget || !m_homeWorkspace || m_restoringHomeTab)
        return;

    const int homeIndex = m_tabWidget->indexOf(m_homeWorkspace);
    if (homeIndex > 0) {
        m_restoringHomeTab = true;
        m_tabWidget->tabBar()->moveTab(homeIndex, 0);
        m_restoringHomeTab = false;
    }

    syncDocumentTabOrder();
    onTabChanged(m_tabWidget->currentIndex());
}

void MainWindow::setHomeDockMode(bool homeActive)
{
    const QList<QDockWidget*> editingDocks = {
        m_histogramDock, m_propsDock, m_colorDock, m_swatchesDock,
        m_historyDock, m_layersDock, m_channelsDock, m_agentDock,
        m_brushSettingsDock, m_brushPanelDock, m_customShapesPanelDock,
        m_generativeFillDock
    };

    if (homeActive) {
        if (!m_homeDockMode) {
            // QMainWindow's native state includes visibility, tabification,
            // dock sizes and floating geometry. Keeping it only in memory
            // makes the Home layout temporary rather than a workspace change.
            m_documentDockState = saveState(WorkspaceManager::kStateVersion);
            m_homeDockMode = true;
        }
        for (QDockWidget* dock : editingDocks) {
            if (dock)
                dock->hide();
        }
        return;
    }

    if (!m_homeDockMode)
        return;

    m_homeDockMode = false;
    if (!m_documentDockState.isEmpty()) {
        const QByteArray snapshot = m_documentDockState;
        m_documentDockState.clear();
        // A snapshot that no longer restores (a workspace preset parked here
        // while Home was active, saved by an older build) must not leave the
        // user with whatever half-state Qt stopped at.
        if (restoreState(snapshot, WorkspaceManager::kStateVersion))
            scheduleRestoredDockChromeRefresh();
        else
            resetDockLayout();
    }
}

void MainWindow::onTabChanged(int index)
{
    clearSelectRefinePreview();
    if (m_canvas && m_canvas->isFreeTransformActive()) {
        m_canvas->cancelFreeTransform();
    }

    const int documentIndex = documentIndexForTabWidget(index);
    setHomeDockMode(documentIndex < 0);
    if (documentIndex >= 0 && documentIndex < static_cast<int>(m_tabs.size())) {
        m_activeTabIndex = documentIndex;
        m_doc = m_tabs[documentIndex].document.get();
        m_canvas = m_tabs[documentIndex].canvas;
        m_ctrl = m_tabs[documentIndex].controller.get();

        m_toolExecutor->setController(m_ctrl);
        m_layerPanel->setController(m_ctrl);
        syncChannelsPanel();
        m_histogramPanel->setController(m_ctrl);
        syncGenerativeFillPanelController();

        if (m_canvas->currentTool() == CanvasView::Tool::AiRemove) {
            // Temporarily hidden: AI Remove Tool will be released later.
            // Do not remove the implementation. Only the user-facing UI is disabled.
            m_canvas->setTool(CanvasView::Tool::Move);
        }

        if (m_canvas->currentTool() == CanvasView::Tool::Move)
        applyMoveOptionsPage();
    else
        m_toolBar->setTool(static_cast<int>(m_canvas->currentTool()));
        // Re-point the AI options page at the newly active canvas's controller.
        if (m_canvas->currentTool() == CanvasView::Tool::AiSelect) {
            m_toolBar->setAiToolPageMode(AiToolPageMode::ObjectSelection);
            m_toolBar->bindAiController(m_canvas->aiSelectionController());
            ensureAiControllerConnected();
        }
        // Temporarily hidden: AI Remove Tool will be released later.
        // Do not remove the implementation. Only the user-facing UI is disabled.
        /*
        else if (m_canvas->currentTool() == CanvasView::Tool::AiRemove) {
            m_toolBar->setAiToolPageMode(AiToolPageMode::RemoveObject);
            m_toolBar->bindAiRemoveController(m_canvas->aiRemoveController());
        }
        */
        updateToolBarLayout();
        // The Align page depends on the active document's tool + active layer.
        updateAlignBarState();
        // Sync toolbar move-tool settings to the incoming canvas so transform
        // controls and auto-select always reflect the current toolbar state,
        // regardless of what state the canvas was in before the tab switch.
        m_canvas->setAutoSelect(m_toolBar->moveAutoSelect());
        m_canvas->setAutoSelectGroup(m_toolBar->moveAutoSelectTarget() == 1);
        m_canvas->setShowTransformControls(m_toolBar->showTransformControls());
        updateDistortControls();
        if (m_colorEngine) {
            m_canvas->setBrushColor(m_colorEngine->foregroundColor());
            m_canvas->setFillBucketColor(m_colorEngine->foregroundColor());
            m_canvas->setShapeFillColor(m_colorEngine->foregroundColor());
        }
        onZoomChanged(m_doc->zoom);

        refreshPropertiesPanel();
        refreshHistoryPanel();
        updateStatusFromDocument();
        updateLayerMenuState();
        updateDirtyUi();
    } else {
        m_activeTabIndex = -1;
        m_doc = nullptr;
        m_canvas = nullptr;
        m_ctrl = nullptr;

        m_toolExecutor->setController(nullptr);
        m_layerPanel->setController(nullptr);
        syncChannelsPanel();
        m_histogramPanel->setController(nullptr);
        m_propsPanel->setController(nullptr);
        syncGenerativeFillPanelController();
        m_propsPanel->clear();
        m_historyPanel->clear();
        if (m_zoomCombo) {
            m_zoomCombo->setZoom(1.0f);
            m_zoomCombo->setEnabled(false);
        }
        m_coordLabel->setText(tr("X: —  Y: —"));
        m_sizeLabel->setText(tr("Size: —"));
        updateLayerMenuState();
        updateDirtyUi();
    }
}

void MainWindow::onTabCloseRequested(int index)
{
    CrashHandler::breadcrumb("onTabCloseRequested: tabWidget index", index);
    index = documentIndexForTabWidget(index);
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) return;

    const auto container = m_tabs[index].container;
    const bool closingRoot = container && m_tabs[index].docId == container->rootId();

    // Closing a project closes the tabs editing its Smart Object contents too,
    // and those hold isolated working copies — so each one is asked FIRST, in
    // its own terms ("commit into the Smart Object?"). The order matters: a
    // commit makes the project unsaved, and the project's own prompt, right
    // after, is then the one that offers to write it to disk.
    if (closingRoot) {
        for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
            if (i == index || m_tabs[i].container != container)
                continue;
            if (!ensureTabCanBeDiscardedOrSaved(i))
                return;
        }
    }

    if (!ensureTabCanBeDiscardedOrSaved(index)) return;
    CrashHandler::breadcrumb("onTabCloseRequested: prompts cleared", index);

    // They are views into the same file, and one left open would offer to
    // "Save" a project the user just closed.
    if (closingRoot) {
        for (int i = static_cast<int>(m_tabs.size()) - 1; i >= 0; --i) {
            if (i == index || m_tabs[i].container != container)
                continue;
            closeTabAt(i);
            if (i < index)
                --index;
        }
        // The registry stops handing this file out. The container itself lives
        // on for as long as a Linked reference from another open project still
        // holds a shared_ptr to it — which is exactly the point of refcounting
        // it rather than deleting it here.
        if (!container->path().isEmpty())
            ContainerRegistry::instance().forget(container->path());
    }

    closeTabAt(index);
}

void MainWindow::closeTabAt(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) return;

    CrashHandler::breadcrumb("closeTabAt: enter", index);

    auto& tab = m_tabs[index];
    // The isolated working copy exists for exactly as long as the tab editing
    // it: dropping the session here is what discards uncommitted content edits
    // (the user was asked first, in ensureTabCanBeDiscardedOrSaved) and what
    // makes the next Edit Contents start from the committed document again.
    if (tab.container && tab.docId != tab.container->rootId()) {
        CrashHandler::breadcrumb("closeTabAt: endEditSession");
        tab.container->endEditSession(tab.docId);
    }
    if (tab.canvas) {
        CrashHandler::breadcrumb("closeTabAt: cleanupDocumentLayers");
        tab.canvas->cleanupDocumentLayers();
    }
    const int tabWidgetIndex = tabWidgetIndexForDocument(index);
    QWidget* page = tab.page ? tab.page : m_tabWidget->widget(tabWidgetIndex);

    bool activeTabRemoved = (index == m_activeTabIndex);
    if (m_activeTabIndex > index)
        --m_activeTabIndex;

    // Disconnect LayerPanel and ToolExecutor from the closing tab's controller
    // before erasing m_tabs, while the ImageController is still alive. The
    // properties panel holds the adjustment editors (Curves/Color Balance),
    // which keep a pointer + documentChanged subscription to the controller —
    // detach them here too so they never touch the destroyed controller.
    CrashHandler::breadcrumb("closeTabAt: detach panels");
    m_toolExecutor->setController(nullptr);
    m_layerPanel->setController(nullptr);
    // Detached by hand rather than through syncChannelsPanel(): that helper
    // re-reads m_canvas/m_ctrl, which still point at the tab being closed, so
    // it would ATTACH the panel to the very controller the lines below destroy.
    // ChannelsPanel::setController then disconnects from whatever it last held
    // — freed memory by the time the panel is pointed anywhere else.
    if (m_channelsPanel) {
        m_channelsPanel->setCanvas(nullptr);
        m_channelsPanel->setController(nullptr);
    }
    m_histogramPanel->setController(nullptr);
    if (m_propsPanel)
        m_propsPanel->setController(nullptr);

    CrashHandler::breadcrumb("closeTabAt: removeTab", tabWidgetIndex);
    m_tabWidget->blockSignals(true);
    m_tabWidget->removeTab(tabWidgetIndex);
    m_tabWidget->blockSignals(false);

    CrashHandler::breadcrumb("closeTabAt: delete page");
    delete page;

    CrashHandler::breadcrumb("closeTabAt: erase tab (destroys controller)");
    m_tabs.erase(m_tabs.begin() + index);
    CrashHandler::breadcrumb("closeTabAt: tab erased, remaining",
                             static_cast<long long>(m_tabs.size()));

    if (m_tabs.empty()) {
        m_activeTabIndex = -1;
        m_doc = nullptr;
        m_canvas = nullptr;
        m_ctrl = nullptr;
        m_toolExecutor->setController(nullptr);
        m_layerPanel->setController(nullptr);
        syncChannelsPanel();
        m_histogramPanel->setController(nullptr);
        syncGenerativeFillPanelController();
        m_propsPanel->clear();
        if (m_zoomCombo) {
            m_zoomCombo->setZoom(1.0f);
            m_zoomCombo->setEnabled(false);
        }
        m_coordLabel->setText(tr("X: —  Y: —"));
        m_sizeLabel->setText(tr("Size: —"));
        CrashHandler::breadcrumb("closeTabAt: show Home page");
        m_tabWidget->setCurrentIndex(0);
        updateDirtyUi();
        CrashHandler::breadcrumb("closeTabAt: done (no tabs left)");
    } else {
        if (activeTabRemoved)
            m_activeTabIndex = documentIndexForTabWidget(m_tabWidget->currentIndex());
        if (m_activeTabIndex < 0)
            m_activeTabIndex = 0;
        if (m_activeTabIndex >= static_cast<int>(m_tabs.size()))
            m_activeTabIndex = static_cast<int>(m_tabs.size()) - 1;
        m_doc = m_tabs[m_activeTabIndex].document.get();
        m_canvas = m_tabs[m_activeTabIndex].canvas;
        m_ctrl = m_tabs[m_activeTabIndex].controller.get();
        m_toolExecutor->setController(m_ctrl);
        m_layerPanel->setController(m_ctrl);
        syncChannelsPanel();
        m_histogramPanel->setController(m_ctrl);
        syncGenerativeFillPanelController();
        refreshPropertiesPanel();
        updateStatusFromDocument();
        m_canvas->update();
        updateDirtyUi();
        CrashHandler::breadcrumb("closeTabAt: done (rebound to tab)",
                                 m_activeTabIndex);
    }
}

void MainWindow::createDefaultDocument()
{
    // Kept as the public startup hook for callers built against older versions.
    // "Default" is now the permanent Home page, not an unsaved raster document.
    if (m_tabWidget)
        m_tabWidget->setCurrentIndex(0);
}

void MainWindow::onNewFile()
{
    showNewDocumentDialog();
}

void MainWindow::onHomeTemplateRequested(const QString& name, int width, int height)
{
    if (width <= 0 || height <= 0)
        return;

    DocumentSettings settings;
    settings.name = name;
    settings.width = width;
    settings.height = height;
    settings.unit = QStringLiteral("px");
    showNewDocumentDialog(&settings);
}

void MainWindow::showAppSettings(int page, std::function<void(int)> onFinished)
{
    if (m_settingsDlg) {
        m_settingsDlg->setCurrentPage(page);
        m_settingsDlg->raise();
        m_settingsDlg->activateWindow();
        return;
    }

    auto* dlg = new AppSettingsDialog(page, m_presetManager,
                                      m_agentController ? m_agentController->client() : nullptr,
                                      this);
    m_settingsDlg = dlg;
    if (onFinished)
        connect(dlg, &QDialog::finished, this, std::move(onFinished));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void MainWindow::showNewDocumentDialog(const DocumentSettings* initialSettings)
{
    if (m_newDocumentDlg) {
        m_newDocumentDlg->raise();
        m_newDocumentDlg->activateWindow();
        return;
    }

    // Modeless: the Custom background colour opens a colour picker whose
    // eyedropper samples the canvas, so an already-open document stays
    // clickable. The document is still created only on OK.
    auto* dlg = new NewDocumentDialog(this);
    if (initialSettings)
        dlg->setSettings(*initialSettings);
    m_newDocumentDlg = dlg;
    dlg->setWindowFlags(dlg->windowFlags() | Qt::Tool);
    connect(dlg, &QDialog::accepted, this, [this, dlg]() {
        createDocumentFromSettings(dlg->settings());
    });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void MainWindow::createDocumentFromSettings(const DocumentSettings& s)
{

    double ppi = (s.resolutionUnit.startsWith("Pixels/cm"))
                     ? s.resolution * 2.54
                     : s.resolution;

    auto toInches = [](double v, const QString& u) -> double {
        if (u.startsWith("Inch") || u.startsWith("in")) return v;
        if (u.startsWith("cm"))   return v / 2.54;
        if (u.startsWith("mm"))   return v / 25.4;
        if (u.startsWith("px"))   return v;
        return v;
    };

    int wPx, hPx;
    if (s.unit.startsWith("px")) {
        wPx = static_cast<int>(std::round(s.width));
        hPx = static_cast<int>(std::round(s.height));
    } else {
        wPx = static_cast<int>(std::round(toInches(s.width, s.unit) * ppi));
        hPx = static_cast<int>(std::round(toInches(s.height, s.unit) * ppi));
    }

    QSize pixelSize(wPx, hPx);
    auto& tab = createTab(s.name, pixelSize);
    if (tab.document) {
        tab.document->resolutionDpi = ppi;
        tab.document->colorMode = documentColorModeFromSetting(s.colorMode);
        tab.document->bitDepth = documentBitDepthFromSetting(s.colorMode);
        // Apply the chosen working-space / colour profile to the new document.
        if (s.colorManaged) {
            const ColorProfile prof = ColorProfile::fromKind(s.colorProfileKind);
            if (prof.isValid()) {
                tab.document->setColorProfile(prof);
                tab.document->setProfileSource(ColorProfileSource::GeneratedDefault);
            }
        } else {
            tab.document->setColorProfile(ColorProfile::invalid());
            tab.document->setProfileSource(ColorProfileSource::Missing);
        }
    }

    if (m_ctrl) {
        m_ctrl->newLayer();
        if (s.background != "Transparent") {
            m_ctrl->fillActiveLayer(s.backgroundColor);
        }
    }
    tab.isDirty = false;
    tab.currentProjectPath.clear();

    refreshLayerPanel();
    refreshPropertiesPanel();
    updateStatusFromDocument();
    updateLayerMenuState();
    m_canvas->update();
    updateDirtyUi();
}

void MainWindow::onOpenFile()
{
    // Build a single combined filter: all image formats + project files
    // (*.hazor, plus the old *.hzs extension, still readable)
    QString combinedFilter = imageCodecRegistry().openFileFilter()
        .section(QStringLiteral(";;"), 0, 0);
    const int parenClose = combinedFilter.lastIndexOf(QLatin1Char(')'));
    if (parenClose != -1)
        combinedFilter.insert(parenClose, QStringLiteral(" *.hazor *.HAZOR *.hzs *.HZS *.psd *.PSD *.psb *.PSB"));

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Project or Image"), {}, combinedFilter);
    if (path.isEmpty()) return;

    openPathAsNewTab(path);
}

void MainWindow::openPathAsNewTab(const QString& path)
{
    if (path.endsWith(".hazor", Qt::CaseInsensitive) || path.endsWith(".hzs", Qt::CaseInsensitive)) {
        if (!loadProjectAsNewTab(path)) {
            AppMessageDialog::warning(this, tr("Open Project"), tr("Failed to open project."));
        }
        return;
    }

    // RAW files get their own non-destructive RAW Layer (Fase 5) instead of
    // the generic codec path below, which would flatten them on import.
    if (RawEngine::isSupportedExtension(
            normalizeImageExtension(QFileInfo(path).suffix()).toStdString())) {
        openRawFileAsNewTab(path);
        return;
    }

    // PSD/PSB are their own import pipeline (PsdImportService), not a codec:
    // they carry layers, masks, text, shapes and adjustments the generic
    // codec path would flatten away.
    if (path.endsWith(".psd", Qt::CaseInsensitive) || path.endsWith(".psb", Qt::CaseInsensitive)) {
        openPsdFileAsNewTab(path);
        return;
    }

    openImageFileAsNewTab(path);
}

void MainWindow::onOpenRecentFile(const QString& path)
{
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        if (!m_tabs[i].sourcePath.isEmpty() && m_tabs[i].sourcePath == absolutePath) {
            m_tabWidget->setCurrentIndex(tabWidgetIndexForDocument(i));
            return;
        }
    }

    if (!QFileInfo(path).isFile()) {
        const QList<AppMessageDialog::ButtonSpec> buttons = {
            { tr("Remove from List"), QMessageBox::DestructiveRole },
            { tr("OK"), QMessageBox::AcceptRole },
        };
        const int clicked = AppMessageDialog::choice(
            this, tr("File Not Found"),
            tr("\"%1\" could not be found.").arg(QFileInfo(path).fileName()),
            AppMessageDialog::Icon::Warning, buttons, /*defaultIndex=*/1,
            tr("It may have been moved, renamed, or deleted. Remove it from the "
               "Recent Files list?"));
        if (clicked == 0)
            RecentFilesManager::instance()->removeFile(path);
        return;
    }

    openPathAsNewTab(path);
}

void MainWindow::rebuildOpenRecentMenu()
{
    m_openRecentMenu->clear();

    const QStringList files = RecentFilesManager::instance()->files();
    if (files.isEmpty()) {
        QAction* noneAction = m_openRecentMenu->addAction(tr("No Recent Files"));
        noneAction->setEnabled(false);
    } else {
        for (const QString& path : files) {
            QAction* action = m_openRecentMenu->addAction(QFileInfo(path).fileName());
            action->setToolTip(path);
            connect(action, &QAction::triggered, this, [this, path]() {
                onOpenRecentFile(path);
            });
        }
    }

    m_openRecentMenu->addSeparator();
    QAction* clearAction = m_openRecentMenu->addAction(tr("Clear Recent Files"));
    clearAction->setEnabled(!files.isEmpty());
    connect(clearAction, &QAction::triggered, this, []() {
        RecentFilesManager::instance()->clear();
    });
}

void MainWindow::openImageFileAsNewTab(const QString& path)
{
    beginProgressDelayed(tr("Progress"), tr("Opening Image"), false);
    auto* watcher = new QFutureWatcher<ImageLoadResult>(this);
    connect(watcher, &QFutureWatcher<ImageLoadResult>::finished, this, [this, watcher, path]() {
        const ImageLoadResult loaded = watcher->result();
        watcher->deleteLater();
        closeProgress();

        if (!loaded.ok) {
            AppMessageDialog::warning(this, tr("Could Not Open Image"), loaded.errorMessage);
            return;
        }

        const std::optional<ColorManagedOpenResult> colorManaged =
            prepareImageForOpenWithDialogs(
                this,
                loaded.image,
                ColorManagementService::instance().defaultOpenContext(path));
        if (!colorManaged)
            return;

        QImage preview = convertDocumentImageToQImage(colorManaged->image).convertToFormat(QImage::Format_RGBA8888);
        if (preview.isNull()) {
            AppMessageDialog::warning(this, tr("Could Not Open Image"),
                tr("The image was decoded but could not be converted for the current document pipeline."));
            return;
        }

        m_tabWidget->setUpdatesEnabled(false);
        const QString title = QFileInfo(path).fileName();
        auto& tab = createTab(title, QSize(preview.width(), preview.height()));
        if (m_doc) {
            m_doc->setColorProfile(colorManaged->finalDocumentProfile);
            m_doc->setProfileSource(colorManaged->finalDocumentProfile.source());
        }

        if (m_ctrl) {
            auto node = std::make_unique<LayerTreeNode>();
            node->type = LayerTreeNode::Type::Layer;
            node->name = title;
            node->layer = std::make_shared<Layer>();
            node->layer->name = title;
            node->layer->cpuImage = preview;
            node->layer->owner = node.get();
            // Required: without this, "Reset Position" stores the post-first-move position
            // as the origin instead of resetting to the original on the first click.
            node->layer->resetTransform = node->transform;
            node->layer->hasResetTransform = true;
            m_doc->selection.resize(preview.width(), preview.height());
            m_doc->roots.push_back(std::move(node));
            m_doc->activeFlatIndex = 0;
            // Required: Move tool reads selectedFlatIndices via collectTransformableSelectedIndices.
            // Without this, m_multiMoveIndices stays empty and m_moving never becomes true,
            // so the layer cannot be moved until the user deselects and reselects it.
            m_doc->selectedFlatIndices.clear();
            m_doc->selectedFlatIndices.insert(0);
            m_canvas->syncLayersToGpu();
            // Fit the (possibly large) image into the viewport instead of showing
            // it over-zoomed at 100%. Deferred internally if the new tab's canvas
            // isn't laid out yet.
            m_canvas->fitToView();
            m_canvas->update();
        }

        tab.currentProjectPath.clear();
        tab.sourcePath = QFileInfo(path).absoluteFilePath();
        tab.isDirty = false;
        RecentFilesManager::instance()->addFile(path);
        m_tabWidget->setUpdatesEnabled(true);
        updateStatusFromDocument();
        if (!colorManaged->warningMessage.isEmpty())
            showViewportStatusMessage(colorManaged->warningMessage, 7000);
        refreshLayerPanel();
        refreshPropertiesPanel();
        updateLayerMenuState();
        updateDirtyUi();
    });
    watcher->setFuture(QtConcurrent::run([path]() {
        return imageCodecRegistry().readImage(path);
    }));
}

namespace {
// Worker-thread result for openRawFileAsNewTab()'s QtConcurrent job: unlike
// the generic open path, this carries the pieces a RawLayerData needs
// (metadata, PP3, develop geometry), not just a flattened DocumentImage.
struct RawOpenResult {
    bool ok = false;
    QString errorMessage;
    QImage image;              // Format_RGBA8888 (RawDevelopService::convertToRgba8888)
    RawMetadata metadata;
    QString pp3;
    QSize developedSize;
    RawGeometry geometry;
    // The session this develop ran on, kept alive instead of destroyed with the
    // worker: opening a RAW goes straight into Hazor Develop, which needs a
    // decoded session of its own, and decoding the same file twice in a row is
    // seconds of CPU and a second few-hundred-MB allocation for nothing. Makes
    // this result move-only, hence takeResult() below.
    std::unique_ptr<RawSession> session;
};
} // namespace

void MainWindow::openRawFileAsNewTab(const QString& path)
{
    if (!RawEngine::isAvailable()) {
        AppMessageDialog::warning(this, tr("Open RAW Image"),
            tr("RAW support is not built into this copy of Hazor."));
        return;
    }

    beginProgressDelayed(tr("Progress"), tr("Opening RAW Image"), false);
    auto* watcher = new QFutureWatcher<RawOpenResult>(this);
    connect(watcher, &QFutureWatcher<RawOpenResult>::finished, this, [this, watcher, path]() {
        // takeResult(), not result(): RawOpenResult carries the opened
        // RawSession, so the future's result is move-only (same as the PSD path).
        RawOpenResult loaded = watcher->future().takeResult();
        watcher->deleteLater();
        closeProgress();

        if (!loaded.ok) {
            AppMessageDialog::warning(this, tr("Could Not Open RAW Image"), loaded.errorMessage);
            return;
        }

        const QImage& preview = loaded.image;
        m_tabWidget->setUpdatesEnabled(false);
        const QString title = QFileInfo(path).fileName();
        auto& tab = createTab(title, QSize(preview.width(), preview.height()));
        if (m_doc) {
            // Camera defaults, no colour-management dialog (Fase 5 scope):
            // rtengine always bakes its own output ICC profile (sRGB by
            // default) into renderFull()'s result, so there is no embedded-
            // profile question to ask the user about here.
            m_doc->setColorProfile(ColorProfile::sRgb());
            m_doc->setProfileSource(ColorProfileSource::GeneratedDefault);
        }

        if (m_ctrl) {
            auto node = std::make_unique<LayerTreeNode>();
            node->type = LayerTreeNode::Type::Layer;
            node->name = title;
            node->layer = std::make_shared<Layer>();
            node->layer->name = title;
            node->layer->cpuImage = preview;
            node->layer->owner = node.get();
            node->layer->resetTransform = node->transform;
            node->layer->hasResetTransform = true;

            auto source = std::make_shared<RawSource>();
            source->path = path;
            source->meta = loaded.metadata;
            auto rawData = std::make_shared<RawLayerData>();
            rawData->source = std::move(source);
            rawData->state.pp3 = loaded.pp3;
            rawData->state.developedSize = loaded.developedSize;
            rawData->state.geometry = loaded.geometry;
            rawData->mode = RawStorageMode::Linked;
            node->layer->rawData = std::move(rawData);

            m_doc->selection.resize(preview.width(), preview.height());
            m_doc->roots.push_back(std::move(node));
            m_doc->activeFlatIndex = 0;
            m_doc->selectedFlatIndices.clear();
            m_doc->selectedFlatIndices.insert(0);
            m_canvas->syncLayersToGpu();
            m_canvas->fitToView();
            m_canvas->update();
        }

        tab.currentProjectPath.clear();
        tab.sourcePath = QFileInfo(path).absoluteFilePath();
        tab.isDirty = false;
        RecentFilesManager::instance()->addFile(path);
        m_tabWidget->setUpdatesEnabled(true);
        updateStatusFromDocument();
        refreshLayerPanel();
        refreshPropertiesPanel();
        updateLayerMenuState();
        updateDirtyUi();
        // Opening a RAW file always means developing it, so go straight to
        // Hazor Develop instead of waiting for a thumbnail double-click — and
        // hand it the session this open already decoded.
        if (m_ctrl) openHazorDevelop(0, std::move(loaded.session));
    });
    watcher->setFuture(QtConcurrent::run([path]() {
        RawEngine::applyBackgroundThreadPolicy();
        RawOpenResult result;
        std::string errorMessage;
        std::unique_ptr<RawSession> session = RawEngine::openFile(path.toStdString(), &errorMessage);
        if (!session) {
            result.errorMessage = QString::fromStdString(errorMessage);
            return result;
        }

        RawDevelopParams params = RawDevelopParams::cameraDefaults();
        const RawImageBuffer buffer = session->renderFull(params);
        if (buffer.isEmpty()) {
            result.errorMessage = QStringLiteral("RAW develop failed.");
            return result;
        }

        result.ok = true;
        result.image = RawDevelopService::convertToRgba8888(buffer, /*dither=*/false);
        result.metadata = session->metadata();
        result.pp3 = QString::fromStdString(params.toPP3String());
        result.developedSize = QSize(buffer.width, buffer.height);
        result.geometry = buffer.geometry;
        result.session = std::move(session);
        return result;
    }));
}

namespace {
// Worker-thread result for openPsdFileAsNewTab()'s QtConcurrent job: parses
// the file and converts it in one background pass (PsdImportService stays
// independent of the UI thread entirely).
struct PsdOpenResult {
    bool ok = false;
    QString errorMessage;
    PsdImportResult import;
};
} // namespace

void MainWindow::openPsdFileAsNewTab(const QString& path)
{
    beginProgressDelayed(tr("Importing PSD"), tr("Converting…"), false);
    auto* watcher = new QFutureWatcher<PsdOpenResult>(this);
    connect(watcher, &QFutureWatcher<PsdOpenResult>::finished, this, [this, watcher, path]() {
        // takeResult(), not result(): PsdImportResult::roots is a vector of
        // unique_ptr, so the future's result is move-only.
        PsdOpenResult loaded = watcher->future().takeResult();
        watcher->deleteLater();
        closeProgress();

        if (!loaded.ok) {
            AppMessageDialog::warning(this, tr("Could Not Open PSD"), loaded.errorMessage);
            return;
        }

        PsdImportResult& imp = loaded.import;
        m_tabWidget->setUpdatesEnabled(false);
        const QString title = QFileInfo(path).fileName();
        auto& tab = createTab(title, imp.canvasSize);
        if (m_doc) {
            m_doc->resolutionDpi = imp.resolutionDpi;
            m_doc->colorMode = imp.colorMode;
            m_doc->bitDepth = imp.bitDepth;
            m_doc->setColorProfile(imp.colorProfile);
            m_doc->setProfileSource(imp.profileSource);
            m_doc->guideManager.setGuides(std::move(imp.guides));
            m_doc->selection.resize(imp.canvasSize.width(), imp.canvasSize.height());
            // addChannel() rather than a plain assignment: it allocates the
            // ids, sizes each buffer to the canvas and keeps the document's id
            // counter ahead of them, so a channel created after the import
            // cannot collide with an imported one.
            for (auto& channel : imp.channels)
                m_doc->addChannel(std::move(channel));
            m_doc->roots = std::move(imp.roots);
            m_doc->activeFlatIndex = m_doc->roots.empty() ? -1 : 0;
            m_doc->selectedFlatIndices.clear();
            if (!m_doc->roots.empty())
                m_doc->selectedFlatIndices.insert(0);
        }
        // Smart Object layers each got a freshly built content Document;
        // register them in this tab's own container now that it exists
        // (PsdImportResult has none of its own — see PsdImportService.hpp).
        if (tab.container) {
            for (auto& [docId, content] : imp.embeddedSmartObjects)
                tab.container->addDocumentWithId(docId, content);
        }
        if (m_ctrl)
            m_ctrl->refreshAllSmartObjectInstances(/*afterLoad=*/true);
        if (m_canvas) {
            m_canvas->syncLayersToGpu();
            m_canvas->fitToView();
            m_canvas->update();
        }

        tab.currentProjectPath.clear();
        tab.sourcePath = QFileInfo(path).absoluteFilePath();
        tab.isDirty = false;
        RecentFilesManager::instance()->addFile(path);
        m_tabWidget->setUpdatesEnabled(true);
        updateStatusFromDocument();
        refreshLayerPanel();
        refreshPropertiesPanel();
        updateLayerMenuState();
        updateDirtyUi();

        if (!imp.warnings.isEmpty()) {
            AppMessageDialog::warning(this, tr("PSD Import — Compatibility Report"),
                tr("The document was imported successfully, but some features are not supported "
                   "by Hazor Studio."),
                imp.warnings.join(QLatin1Char('\n')));
        }
    });
    watcher->setFuture(QtConcurrent::run([path]() {
        PsdOpenResult result;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            result.errorMessage = tr("Could not read \"%1\".").arg(QFileInfo(path).fileName());
            return result;
        }
        result.import = PsdImportService::importFromBytes(file.readAll());
        result.ok = result.import.ok;
        if (!result.ok)
            result.errorMessage = result.import.errorMessage;
        return result;
    }));
}

void MainWindow::openHazorDevelop(int flatIndex, std::unique_ptr<RawSession> session)
{
    if (!m_ctrl || !m_doc) return;
    auto* node = m_doc->nodeAt(flatIndex);
    if (!node || node->type != LayerTreeNode::Type::Layer || !node->layer || !node->layer->rawData)
        return;

    m_ctrl->setActiveNode(flatIndex);

    // One window per layer: a second double-click on the same thumbnail must
    // raise the one already open, not start a second RawSession on the same
    // file whose Apply would race the first one's.
    if (auto existing = m_developWindows.constFind(node); existing != m_developWindows.constEnd()) {
        (*existing)->show();
        (*existing)->raise();
        (*existing)->activateWindow();
        return;
    }

    auto* develop = new RawDevelopWindow(*node->layer->rawData, node->layer->cpuImage, this,
                                        std::move(session));
    m_developWindows.insert(node, develop);
    connect(develop, &QObject::destroyed, this, [this, node]() { m_developWindows.remove(node); });

    // QPointer, not a raw one: this window outlives nothing it owns, but the
    // document's tab can be closed from under it while it is open, and Apply
    // would then reach a destroyed controller.
    QPointer<ImageController> controller = m_ctrl;
    // `node` is the layer's identity for the whole exchange. Hazor Develop is
    // an independent window, so the user can reorder, insert or delete layers
    // in the main window while it is open: a flat index captured here would
    // silently re-reveal a *different* layer, and the completion filter below
    // would never match the index ImageController re-resolves.
    connect(develop, &RawDevelopWindow::applyRequested, develop,
            [this, develop, controller, node](const RawDevelopParams& params) {
        // Resolved against the controller's own document, not the active
        // tab's: Hazor Develop keeps working while the user is looking at
        // another document.
        const int liveIndex = controller ? flatIndexOfNodeIn(controller->document(), node) : -1;
        if (liveIndex < 0) {
            AppMessageDialog::warning(develop, tr("Hazor Develop"),
                tr("This RAW layer is no longer in its document."));
            return;
        }
        // Donate Hazor Develop's warm preview session to the develop service
        // BEFORE dispatching: requestDevelop() decides there and then whether it
        // has to call RawEngine::openFile(), and the file this window has had
        // decoded all along is the same one. Without this, Apply paid a second
        // full decode — seconds of CPU and a few hundred MB resident next to the
        // session still held by this (not yet destroyed) window, which is most of
        // why Apply felt like the machine had stalled.
        //
        // Declined when a preview is still rendering (takeIdleSession()), and
        // harmless on the failure path below: the session stays cached on the
        // node for the retry, only this window's own next preview would re-decode.
        QString donatedTempPath;
        if (auto warm = develop->takeIdleSession(&donatedTempPath))
            controller->adoptRawSession(liveIndex, std::move(warm), donatedTempPath);

        if (!controller->rerevealRawLayer(liveIndex, params)) {
            AppMessageDialog::warning(develop, tr("Hazor Develop"),
                tr("Could not start RAW development. The layer may be locked or already processing."));
            return;
        }

        // The render itself already runs off the UI thread (RawDevelopService);
        // what froze the interface was keeping Develop open and disabled for
        // the whole wait. Close it right away and hand the wait off to the
        // main window's own loading dialog instead, same as opening a RAW file.
        develop->close();
        beginProgressDelayed(tr("Progress"), tr("Applying RAW Changes…"), false);

        // Self-disconnecting rather than tied to `develop` (already gone) or
        // to `this` for the controller's lifetime (would accumulate one stale
        // connection per Apply click): fires once for *this* node's result,
        // ignoring any other RAW layer's re-reveal that finishes first, then
        // detaches itself.
        auto connection = std::make_shared<QMetaObject::Connection>();
        *connection = connect(controller, &ImageController::rawLayerDevelopFinished, this,
                [this, node, connection](LayerTreeNode* completed, int, bool ok, const QString& message) {
            if (completed != node)
                return;
            disconnect(*connection);
            closeProgress();
            if (!ok) {
                AppMessageDialog::warning(this, tr("Hazor Develop"),
                    message.isEmpty() ? tr("RAW development failed.") : message);
                return;
            }
            if (!message.isEmpty())
                AppMessageDialog::warning(this, tr("Hazor Develop"), message);
        });
    });
    develop->show();
    develop->raise();
    develop->activateWindow();
}

int MainWindow::tabIndexForDocument(const DocumentContainer* container,
                                    const SmartObjectDocId& docId) const
{
    if (!container || docId.isEmpty())
        return -1;
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        if (m_tabs[i].container.get() == container && m_tabs[i].docId == docId)
            return i;
    }
    return -1;
}

int MainWindow::flatIndexOfNodeIn(const Document* doc, const LayerTreeNode* node)
{
    if (!doc || !node)
        return -1;
    const auto flat = doc->flatten();
    const auto it = std::find(flat.begin(), flat.end(), node);
    return it == flat.end() ? -1 : static_cast<int>(it - flat.begin());
}

void MainWindow::showProgress(const QString& title, const QString& message, bool cancelable)
{
    if (!m_progressDialog) {
        m_progressDialog = new ProgressDialog(this);
        connect(m_progressDialog, &ProgressDialog::cancelRequested,
                this, &MainWindow::handleProgressCancel);
    }

    m_progressDialog->setWindowTitle(title.isEmpty() ? tr("Progress") : title);
    m_progressDialog->setMessage(message);
    m_progressDialog->setCancelable(cancelable);
    m_progressDialog->setProgressValue(-1);
    m_progressDialog->show();
    m_progressDialog->raise();
    m_progressDialog->activateWindow();
}

void MainWindow::beginProgressDelayed(const QString& title, const QString& message, bool cancelable)
{
    m_progressActive = true;
    m_pendingProgressTitle = title;
    m_pendingProgressMessage = message;
    m_pendingProgressCancelable = cancelable;

    QApplication::setOverrideCursor(Qt::BusyCursor);

    if (!m_progressShowTimer) {
        m_progressShowTimer = new QTimer(this);
        m_progressShowTimer->setSingleShot(true);
        connect(m_progressShowTimer, &QTimer::timeout, this, [this]() {
            if (m_progressActive)
                showProgress(m_pendingProgressTitle, m_pendingProgressMessage,
                             m_pendingProgressCancelable);
        });
    }

    if (m_progressDialog)
        m_progressDialog->hide();
    m_progressShowTimer->start(220);
}

void MainWindow::setProgress(int value)
{
    if (m_progressDialog && m_progressDialog->isVisible())
        m_progressDialog->setProgressValue(value);
}

void MainWindow::setMessage(const QString& message)
{
    m_pendingProgressMessage = message;
    if (m_progressDialog && m_progressDialog->isVisible())
        m_progressDialog->setMessage(message);
}

void MainWindow::closeProgress()
{
    m_progressActive = false;
    m_activeProgressJobId = 0;
    m_progressController.clear();
    if (m_progressShowTimer)
        m_progressShowTimer->stop();
    if (m_progressDialog)
        m_progressDialog->hide();
    QApplication::restoreOverrideCursor();
}

bool MainWindow::wasCanceled() const
{
    return m_progressDialog && m_progressDialog->wasCanceled();
}

void MainWindow::handleProgressCancel()
{
    if (m_activeProgressJobId != 0 && m_progressController)
        m_progressController->cancelLongOperation(m_activeProgressJobId);
}

void MainWindow::onSaveFile()
{
    saveProjectForTab(m_activeTabIndex, false);
}

void MainWindow::onSaveAsFile()
{
    saveProjectForTab(m_activeTabIndex, true);
}

bool MainWindow::saveProjectForTab(int index, bool saveAs)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size()))
        return false;
    if (!m_tabs[index].document)
        return false;

    // Ctrl+S in a content tab does NOT write the project to disk: it COMMITS
    // the tab's isolated working copy into the embedded Smart Object, updates
    // every instance and marks the project as needing a save. Saving the
    // contents and writing the project are two different acts. Writing the
    // whole .hazor here would also flush unrelated edits the user made in the
    // main document and had not chosen to save yet.
    if (m_tabs[index].container
        && m_tabs[index].docId != m_tabs[index].container->rootId()) {
        if (!saveAs)
            return commitEmbeddedContent(index);
        // Save As from a content tab still means "write the project somewhere
        // else" — the container is the only thing there is a file for, and
        // writing the contents alone is Convert to Linked. But the file has to
        // match what the user is looking at, so the isolated edits are
        // committed first instead of being silently left out of the copy.
        if (m_tabs[index].isDirty && !commitEmbeddedContent(index))
            return false;
    }

    // Save is a CONTAINER operation, not a document one: a .hazor holds the root
    // document plus every embedded Smart Object document, and one file has one
    // save path. The path/dirty bookkeeping belongs to the root tab — the tab
    // that owns the file name.
    const int targetIndex = rootTabIndexForContainer(m_tabs[index].container.get(), index);
    auto& tab = m_tabs[targetIndex];
    const auto container = tab.container;

    QString path = tab.currentProjectPath;
    const bool choosingNewPath = saveAs || path.isEmpty();
    if (choosingNewPath) {
        QString suggested = tab.name;
        if (!suggested.endsWith(".hazor", Qt::CaseInsensitive))
            suggested += ".hazor";
        path = QFileDialog::getSaveFileName(
            this, tr("Save Project As"), suggested,
            tr("Hazor Studio (*.hazor)"));
        if (path.isEmpty())
            return false;
        if (!path.endsWith(".hazor", Qt::CaseInsensitive))
            path += ".hazor";
    }

    // Linked Smart Object paths are stored relative to the .hazor that holds
    // them, so writing the project somewhere else has to rewrite them BEFORE
    // the save — otherwise the file goes out with links resolved against a
    // location it no longer has. Not an undo step: it preserves the exact same
    // references, expressed from the file's new home.
    if (container && choosingNewPath)
        smartobject::rebaseLinkedPaths(*container, path);

    beginProgressDelayed(tr("Progress"), tr("Saving Document"), false);

    QFutureWatcher<QPair<bool, QString>> watcher;
    QEventLoop loop;
    connect(&watcher, &QFutureWatcher<QPair<bool, QString>>::finished,
            &loop, &QEventLoop::quit);
    // The container is captured by shared_ptr so the worker cannot outlive it.
    watcher.setFuture(QtConcurrent::run(
        [container, doc = tab.document.get(), path]() {
        QString err;
        const bool ok = container
            ? ProjectFileService::saveContainer(*container, path, &err)
            : ProjectFileService::saveProject(*doc, path, &err);
        return QPair<bool, QString>(ok, err);
    }));
    loop.exec();

    const auto result = watcher.result();
    closeProgress();

    if (!result.first) {
        AppMessageDialog::warning(this, tr("Save Project"),
            result.second.isEmpty() ? tr("Failed to save project.") : result.second);
        return false;
    }

    // Re-key the registry before anything else: after a Save As the container is
    // reachable at the new path and must no longer answer to the old one.
    if (container)
        ContainerRegistry::instance().registerContainer(container, path);

    tab.currentProjectPath = path;
    tab.name = QFileInfo(path).fileName();
    tab.isDirty = false;
    if (choosingNewPath) {
        tab.sourcePath = QFileInfo(path).absoluteFilePath();
        RecentFilesManager::instance()->addFile(path);
    }
    // Other tabs of the same container were written along with it — but NOT a
    // content tab with uncommitted edits: those live in a working copy the save
    // deliberately did not pick up, so its asterisk has to stay. What reached
    // the disk is everything that had been committed.
    for (auto& other : m_tabs) {
        if (other.container != container)
            continue;
        if (other.isDirty && other.docId != container->rootId())
            continue;
        other.isDirty = false;
    }
    updateDirtyUi();

    if (m_homeWorkspace)
        m_homeWorkspace->storeDocumentThumbnail(path, tab.document.get());

    // A project that other open projects LINK to just changed on disk. Their
    // instances have to re-render, and each of them is then unsaved in its own
    // right: the cached render is part of a .hazor, so a dependent's file no
    // longer matches what is in memory. Their files are deliberately NOT
    // written — only the project the user asked to save was.
    scheduleSmartObjectRefresh();
    return true;
}

bool MainWindow::commitEmbeddedContent(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size()))
        return false;
    auto& tab = m_tabs[index];
    if (!tab.container || !tab.controller)
        return false;

    // Nothing to commit is not a failure, and it must not mark the project
    // unsaved: Ctrl+S on an untouched content tab would otherwise make the
    // whole .hazor look edited for no reason.
    if (!tab.isDirty) {
        showViewportStatusMessage(tr("Smart Object contents are already up to date."), 2000);
        return true;
    }

    // THIS is where the content tab's isolated working copy becomes the
    // project's content: it is copied into the stored document, every instance
    // of it re-renders, and the project starts differing from its file. The
    // commit is driven by the container's ROOT controller — one cascade owner
    // per file, whichever tab asked (see flushSmartObjectRefresh).
    const SmartObjectDocId docId = tab.docId;
    const auto container = tab.container;
    auto* root = rootControllerForContainer(container.get());
    if (!root || !root->commitSmartObjectEditSession(docId)) {
        AppMessageDialog::warning(this, tr("Smart Object"),
            tr("This Smart Object's contents could not be committed: its document "
               "is no longer part of the project."));
        return false;
    }

    tab.isDirty = false;                    // the content is committed
    markContainerRootDirty(container);      // the .hazor is not written yet
    updateDirtyUi();
    showViewportStatusMessage(
        tr("Smart Object contents updated. Save the project to write them to disk."),
        3000);
    return true;
}

void MainWindow::markContainerRootDirty(const std::shared_ptr<DocumentContainer>& container)
{
    if (!container)
        return;
    const int rootTab = tabIndexForDocument(container.get(), container->rootId());
    if (rootTab < 0 || m_tabs[rootTab].isDirty)
        return;
    m_tabs[rootTab].isDirty = true;
    updateDirtyUi();
}

int MainWindow::rootTabIndexForContainer(const DocumentContainer* container,
                                         int fallbackIndex) const
{
    if (!container)
        return fallbackIndex;
    const int rootTab = tabIndexForDocument(container, container->rootId());
    return rootTab >= 0 ? rootTab : fallbackIndex;
}

bool MainWindow::ensureTabCanBeDiscardedOrSaved(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size()))
        return true;
    auto& tab = m_tabs[index];
    if (!tab.isDirty)
        return true;

    // A tab editing a Smart Object's contents edits a working copy that lives
    // only as long as the tab, so closing it DOES discard uncommitted work —
    // and the prompt's "Save" means "commit into the Smart Object", not "write
    // the file". Before isolation there was nothing to ask here; now there is.
    const bool isContentTab = tab.container && tab.docId != tab.container->rootId();

    const QString displayName = isContentTab
        ? tab.name
        : (tab.currentProjectPath.isEmpty() ? tab.name
                                            : QFileInfo(tab.currentProjectPath).fileName());

    const QList<AppMessageDialog::ButtonSpec> buttons = {
        { isContentTab ? tr("Commit") : tr("Save"), QMessageBox::AcceptRole },
        { tr("Discard"), QMessageBox::DestructiveRole },
        { tr("Cancel"), QMessageBox::RejectRole },
    };
    const int clicked = AppMessageDialog::choice(
        this, tr("Unsaved Changes"),
        isContentTab
            ? tr("The Smart Object contents \"%1\" have changes that are not in "
                 "the project yet.").arg(displayName)
            : tr("The project \"%1\" has unsaved changes.").arg(displayName),
        AppMessageDialog::Icon::Warning, buttons, /*defaultIndex=*/0,
        isContentTab ? tr("Do you want to commit them to the Smart Object?")
                     : tr("Do you want to save your changes?"));

    if (clicked == 0)
        return saveProjectForTab(index, false);
    if (clicked == 1)
        return true;
    return false;
}

void MainWindow::markActiveTabDirty(bool dirty)
{
    if (m_activeTabIndex < 0 || m_activeTabIndex >= static_cast<int>(m_tabs.size()))
        return;
    auto& tab = m_tabs[m_activeTabIndex];
    bool changed = tab.isDirty != dirty;
    tab.isDirty = dirty;

    // The dirty mark stops HERE, deliberately: a content tab edits an isolated
    // working copy, so the project it belongs to still matches its file. What
    // marks the project unsaved is the commit (commitEmbeddedContent), not the
    // edit — and the content tab's own asterisk is what says "these changes are
    // not in the project yet". Closing the tab with the mark set prompts, so
    // nothing is lost silently.

    if (changed)
        updateDirtyUi();
}

QString MainWindow::documentFileName(const DocumentTab& tab) const
{
    QString name;
    if (!tab.currentProjectPath.isEmpty())
        name = QFileInfo(tab.currentProjectPath).fileName();
    else if (!tab.sourcePath.isEmpty())
        name = QFileInfo(tab.sourcePath).fileName();
    else if (!tab.name.isEmpty())
        name = tab.name;
    else if (tab.document)
        name = tab.document->name;
    return name.isEmpty() ? tr("Untitled") : name;
}

QString MainWindow::documentTabTitle(const DocumentTab& tab) const
{
    const QString fileName = documentFileName(tab);
    if (!tab.document)
        return tab.isDirty ? fileName + QStringLiteral(" *") : fileName;

    const Document& document = *tab.document;
    const float zoom = std::isfinite(document.zoomTarget)
        ? document.zoomTarget : document.zoom;

    QString layerName;
    if (const auto* node = document.activeNode()) {
        layerName = node->name;
        if (layerName.isEmpty() && node->layer)
            layerName = node->layer->name;
    }

    const QString colorContext = tab.controller && tab.controller->isEditingMask()
        ? tr("Layer Mask")
        : documentColorModeLabel(document.colorMode);
    const QString details = layerName.isEmpty()
        ? QStringLiteral("%1/%2").arg(colorContext).arg(document.bitDepth)
        : QStringLiteral("%1, %2/%3")
              .arg(layerName, colorContext)
              .arg(document.bitDepth);

    return QStringLiteral("%1 @ %2% (%3)%4")
        .arg(fileName, formattedZoomPercent(zoom), details,
             tab.isDirty ? QStringLiteral(" *") : QString());
}

void MainWindow::updateDirtyUi()
{
    // EVERY tab's label is refreshed, not just the active one. A change can now
    // reach a project the user is not looking at — committing a Smart Object's
    // contents marks the main document unsaved, and saving a linked file marks
    // the projects that display it unsaved — and an asterisk that only appears
    // once you switch to the tab is an asterisk nobody sees in time.
    QString activeTitle = tr("Hazor Studio");

    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        const auto& tab = m_tabs[i];
        const QString fullTabTitle = documentTabTitle(tab);
        const int tabWidgetIndex = tabWidgetIndexForDocument(i);
        if (tabWidgetIndex < m_tabWidget->count()) {
            if (m_tabWidget->tabText(tabWidgetIndex) != fullTabTitle)
                m_tabWidget->setTabText(tabWidgetIndex, fullTabTitle);
            if (m_tabWidget->tabToolTip(tabWidgetIndex) != fullTabTitle)
                m_tabWidget->setTabToolTip(tabWidgetIndex, fullTabTitle);
        }
        if (i == m_activeTabIndex) {
            const QString windowDocumentName = documentFileName(tab)
                + (tab.isDirty ? QStringLiteral(" *") : QString());
            activeTitle = QStringLiteral("%1 - Hazor Studio").arg(windowDocumentName);
        }
    }

    setWindowTitle(activeTitle);
}

bool MainWindow::loadProjectAsNewTab(const QString& path)
{
    // Through the registry, never ProjectFileService directly: one container per
    // physical file for the whole application. Opening a .hazor that a Linked
    // Smart Object already pulled in adopts that same container instead of
    // building a second Document for the same bytes (see ContainerRegistry.hpp).
    QString error;
    auto container = ContainerRegistry::instance().open(path, &error);
    if (!container || !container->rootDocument()) {
        AppMessageDialog::warning(this, tr("Open Project"),
            error.isEmpty() ? tr("Invalid project file.") : error);
        return false;
    }

    // A second tab for the same (container, document) would give one Document
    // two CommandHistories and two dirty flags. Reactivate instead — the same
    // rule "Edit Contents" follows for an already-open Smart Object document.
    const int existingTab = tabIndexForDocument(container.get(), container->rootId());
    if (existingTab >= 0) {
        m_tabWidget->setCurrentIndex(tabWidgetIndexForDocument(existingTab));
        RecentFilesManager::instance()->addFile(path);
        return true;
    }

    const QString tabName = QFileInfo(path).fileName();
    // The loaded document is adopted as-is — no clone of the layer tree. The
    // previous code cloned it because loadProject() handed back loose roots the
    // tab had to copy into its own Document; documentFromJson() now builds the
    // real Document, and cloning it here would detach the tab from the
    // container that has to save it.
    auto& tab = createTabForDocument(tabName, container, container->rootId());
    if (!tab.document || !tab.controller)
        return false;

    // NOTE: masks live in the layer's base-pixel space anchored by maskOrigin
    // (see Layer::maskTargetBounds / DocumentCompositor), and ProjectFileService
    // round-trips them exactly. No post-load mask resizing here: a stale
    // "migrate to canvas-sized" pass used to warp every sub-canvas mask up to the
    // document size, which corrupted correctly-saved layer-space masks (the
    // renderer still sampled them at layer size, so the mask shrank on reload).

    tab.currentProjectPath = path;
    tab.sourcePath = QFileInfo(path).absoluteFilePath();
    tab.name = tabName;
    tab.isDirty = false;
    RecentFilesManager::instance()->addFile(path);
    m_activeTabIndex = documentIndexForTabWidget(m_tabWidget->currentIndex());
    m_doc = tab.document.get();
    m_ctrl = tab.controller.get();
    m_canvas = tab.canvas;
    m_canvas->syncLayersToGpu();
    m_canvas->update();
    refreshLayerPanel();
    refreshPropertiesPanel();
    refreshHistoryPanel();
    updateLayerMenuState();
    updateStatusFromDocument();
    updateDirtyUi();
    if (m_homeWorkspace)
        m_homeWorkspace->storeDocumentThumbnail(path, tab.document.get());
    // Validates every Smart Object reference in the file (so a Linked target
    // that moved gets its broken badge) and re-renders only the genuinely stale
    // ones — a persisted render is trusted.
    tab.controller->refreshAllSmartObjectInstances(/*afterLoad=*/true);
    return true;
}

int MainWindow::openProjectFiles(const QStringList& files)
{
    int opened = 0;
    for (const QString& path : files) {
        if (!path.endsWith(QStringLiteral(".hazor"), Qt::CaseInsensitive)
            && !path.endsWith(QStringLiteral(".hzs"), Qt::CaseInsensitive))
            continue;
        if (!QFileInfo(path).isFile()) {
            AppMessageDialog::warning(this, tr("Open Project"),
                tr("File not found:\n%1").arg(path));
            continue;
        }
        if (loadProjectAsNewTab(path))
            ++opened;
    }
    if (opened > 0)
        raise();
    return opened;
}

void MainWindow::runImageExport(const QString& format, const QString& suggestedBaseName,
                                std::function<bool(const QString&, QString*)> save)
{
    QString defaultExt = normalizeImageExtension(format);
    if (defaultExt == QLatin1String("jpeg"))
        defaultExt = QStringLiteral("jpg");
    if (defaultExt.isEmpty())
        defaultExt = QStringLiteral("png");

    // Lock the save dialog to the format chosen in the export dialog, so the
    // user cannot pick a different type here and the extension is applied
    // automatically — like professional editors.
    const QString lockedFilter = imageCodecRegistry().saveFilterForFormat(format);

    QString path = QFileDialog::getSaveFileName(
        this, tr("Export Image"), suggestedBaseName, lockedFilter, nullptr,
        QFileDialog::DontConfirmOverwrite);
    if (path.isEmpty()) return;

    // Ensure the file carries the chosen format's extension. If the user typed
    // no extension, or one that doesn't match the locked format, append the
    // correct one automatically.
    const QString typedSuffix = normalizeImageExtension(QFileInfo(path).suffix());
    const bool suffixMatchesFormat =
        !typedSuffix.isEmpty() &&
        imageCodecRegistry().saveFilterForFormat(typedSuffix) == lockedFilter;
    if (!suffixMatchesFormat)
        path += QStringLiteral(".%1").arg(defaultExt);

    // Manual overwrite check (only same full path)
    if (QFile::exists(path)) {
        if (!AppMessageDialog::confirm(this, tr("Confirm Save As"),
                tr("%1 already exists.\nDo you want to replace it?")
                    .arg(QFileInfo(path).fileName())))
            return;
    }

    beginProgressDelayed(tr("Progress"), tr("Exporting Image"), false);
    auto* watcher = new QFutureWatcher<QPair<bool, QString>>(this);
    connect(watcher, &QFutureWatcher<QPair<bool, QString>>::finished, this, [this, watcher]() {
        const auto result = watcher->result();
        watcher->deleteLater();
        closeProgress();
        if (!result.first)
            AppMessageDialog::warning(this, tr("Could Not Export Image"),
                result.second.isEmpty() ? tr("Failed to export image.") : result.second);
    });
    watcher->setFuture(QtConcurrent::run([path, save]() {
        QString errorMessage;
        const bool ok = save(path, &errorMessage);
        return QPair<bool, QString>(ok, errorMessage);
    }));
}

void MainWindow::onExportFile()
{
    if (!m_doc) return;

    // A raster export is the flattened colour composite: it carries no extra
    // channels, and it deliberately does not promote an alpha channel to the
    // image's transparency (a saved selection is not the image's alpha). Say
    // so before the file is written instead of dropping them silently — the
    // document keeps its channels either way, and .hazor preserves them.
    if (!m_doc->channels.empty()) {
        const QString text = tr("This document has %n extra channel(s), which "
                                "image formats cannot store. They stay in the "
                                "document; the exported file will contain the "
                                "colour composite only.",
                                nullptr, static_cast<int>(m_doc->channels.size()));
        const int answer = AppMessageDialog::choice(
            this, tr("Export"), text, AppMessageDialog::Icon::Warning,
            {{tr("Export Anyway"), QMessageBox::AcceptRole},
             {tr("Cancel"), QMessageBox::RejectRole}}, 0);
        if (answer != 0)
            return;
    }

    ExportImageDialog dlg(m_doc, this);
    if (dlg.exec() != QDialog::Accepted) return;

    auto opts = dlg.options();

    ExportOptions eopts;
    eopts.quality     = opts.quality;
    eopts.compression = opts.compression;
    eopts.progressive = opts.progressive;
    eopts.preserveAlpha = opts.transparency;
    // Encode with the format chosen in the export dialog; the path's suffix is
    // guaranteed to match it (runImageExport locks the save dialog to it).
    eopts.format      = normalizeImageExtension(opts.format);
    eopts.colorMode   = opts.colorMode;
    eopts.colorSelectedProfile = opts.colorSelectedProfile;
    if (opts.resizeEnabled && opts.targetSize.isValid())
        eopts.resizeTo = opts.targetSize;

    Document* doc = m_doc;
    runImageExport(opts.format, QString(), [doc, eopts](const QString& path, QString* errorMessage) {
        return saveImage(doc, path, eopts, errorMessage);
    });
}

void MainWindow::onExportNode(int flatIndex)
{
    if (!m_doc) return;
    LayerTreeNode* node = m_doc->nodeAt(flatIndex);
    if (!node || !node->hasIsolatedExportContent()) {
        AppMessageDialog::information(this, tr("Export Layer"),
            tr("This adjustment has no content of its own to export — it only "
               "modifies the layers beneath it."));
        return;
    }

    ExportImageDialog dlg(m_doc, node, this);
    if (dlg.exec() != QDialog::Accepted) return;

    auto opts = dlg.options();

    ExportOptions eopts;
    eopts.quality     = opts.quality;
    eopts.compression = opts.compression;
    eopts.progressive = opts.progressive;
    eopts.preserveAlpha = opts.transparency;
    eopts.format      = normalizeImageExtension(opts.format);
    eopts.colorMode   = opts.colorMode;
    eopts.colorSelectedProfile = opts.colorSelectedProfile;
    if (opts.resizeEnabled && opts.targetSize.isValid())
        eopts.resizeTo = opts.targetSize;

    Document* doc = m_doc;
    const bool layerBounds = opts.layerBoundsMode;
    runImageExport(opts.format, node->name,
                   [doc, node, eopts, layerBounds](const QString& path, QString* errorMessage) {
        return saveNodeImage(doc, node, path, eopts, layerBounds, errorMessage);
    });
}

void MainWindow::onImportImage()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Image"), {},
        imageCodecRegistry().openFileFilter());
    if (path.isEmpty()) return;

    beginProgressDelayed(tr("Progress"), tr("Importing Image"), false);
    auto* watcher = new QFutureWatcher<ImageLoadResult>(this);
    connect(watcher, &QFutureWatcher<ImageLoadResult>::finished, this, [this, watcher, path]() {
        const ImageLoadResult loaded = watcher->result();
        watcher->deleteLater();
        closeProgress();

        if (!loaded.ok) {
            AppMessageDialog::warning(this, tr("Could Not Import Image"), loaded.errorMessage);
            return;
        }

        OpenColorPolicyContext colorContext =
            ColorManagementService::instance().defaultOpenContext(path);
        if (m_doc) {
            colorContext.workingSpace = m_doc->colorProfile().isValid()
                ? m_doc->colorProfile()
                : ColorProfile::sRgb();
            colorContext.mismatchPolicy = ProfileMismatchPolicy::ConvertToWorkingSpace;
        }
        std::optional<ColorManagedOpenResult> colorManaged =
            prepareImageForOpenWithDialogs(this, loaded.image, colorContext);
        if (!colorManaged)
            return;

        QImage img = convertDocumentImageToQImage(colorManaged->image).convertToFormat(QImage::Format_RGBA8888);
        if (img.isNull()) {
            AppMessageDialog::warning(this, tr("Could Not Import Image"),
                tr("The image was decoded but could not be converted for the current document pipeline."));
            return;
        }

        if (!m_ctrl || !m_doc) {
            auto& tab = createTab(QFileInfo(path).fileName(), img.size());
            if (m_ctrl && m_doc) {
                m_doc->setColorProfile(colorManaged->finalDocumentProfile);
                m_doc->setProfileSource(colorManaged->finalDocumentProfile.source());
                m_ctrl->importImage(std::move(img), QFileInfo(path).completeBaseName());
                tab.isDirty = false;
                tab.currentProjectPath.clear();
                refreshLayerPanel();
                refreshPropertiesPanel();
                updateStatusFromDocument();
                if (!colorManaged->warningMessage.isEmpty())
                    showViewportStatusMessage(colorManaged->warningMessage, 7000);
                updateLayerMenuState();
            }
            return;
        }

        m_ctrl->importImage(std::move(img), QFileInfo(path).completeBaseName());
        if (m_canvas) m_canvas->syncLayersToGpu();
        refreshLayerPanel();
        refreshPropertiesPanel();
        updateLayerMenuState();
        if (!colorManaged->warningMessage.isEmpty())
            showViewportStatusMessage(colorManaged->warningMessage, 7000);
    });
    watcher->setFuture(QtConcurrent::run([path]() {
        return imageCodecRegistry().readImage(path);
    }));
}

void MainWindow::onEditDocument()
{
    if (!m_doc) return;

    m_tabWidget->setUpdatesEnabled(false);
    NewDocumentDialog dlg(this, NewDocumentDialog::Mode::Edit);

    DocumentSettings s;
    s.name = m_doc->name;
    s.width = static_cast<double>(m_doc->size.width());
    s.height = static_cast<double>(m_doc->size.height());
    s.unit = "px";
    s.resolution = static_cast<int>(std::round(m_doc->resolutionDpi));
    s.resolutionUnit = "Pixels/Inch";
    s.colorMode = documentColorModeSetting(m_doc->colorMode, m_doc->bitDepth);
    s.background = "White";
    s.backgroundColor = Qt::white;
    s.colorManaged = m_doc->isColorManaged();
    s.colorProfileKind = m_doc->colorProfile().kind();

    dlg.setSettings(s);

    if (dlg.exec() != QDialog::Accepted) {
        m_tabWidget->setUpdatesEnabled(true);
        return;
    }
    m_tabWidget->setUpdatesEnabled(true);

    s = dlg.settings();

    auto toInches = [](double v, const QString& u) -> double {
        if (u.startsWith("Inch") || u.startsWith("in")) return v;
        if (u.startsWith("cm"))   return v / 2.54;
        if (u.startsWith("mm"))   return v / 25.4;
        if (u.startsWith("px"))   return v;
        return v;
    };

    double ppi = (s.resolutionUnit.startsWith("Pixels/cm"))
                     ? s.resolution * 2.54
                     : s.resolution;

    int wPx, hPx;
    if (s.unit.startsWith("px")) {
        wPx = static_cast<int>(std::round(s.width));
        hPx = static_cast<int>(std::round(s.height));
    } else {
        wPx = static_cast<int>(std::round(toInches(s.width, s.unit) * ppi));
        hPx = static_cast<int>(std::round(toInches(s.height, s.unit) * ppi));
    }
    QSize newSize(wPx, hPx);

    const QString newColorMode = documentColorModeFromSetting(s.colorMode);
    const int newBitDepth = documentBitDepthFromSetting(s.colorMode);
    const bool metadataChanged = m_doc->name != s.name
        || !qFuzzyCompare(m_doc->resolutionDpi, ppi)
        || m_doc->colorMode != newColorMode
        || m_doc->bitDepth != newBitDepth;

    m_doc->name = s.name;
    if (m_activeTabIndex >= 0
        && m_activeTabIndex < static_cast<int>(m_tabs.size())
        && m_tabs[m_activeTabIndex].currentProjectPath.isEmpty()
        && m_tabs[m_activeTabIndex].sourcePath.isEmpty()) {
        m_tabs[m_activeTabIndex].name = s.name;
    }
    m_doc->resolutionDpi = ppi;
    m_doc->colorMode = newColorMode;
    m_doc->bitDepth = newBitDepth;

    if (m_ctrl && m_doc->size != newSize)
        m_ctrl->resizeDocument(newSize);
    else if (m_canvas)
        m_canvas->update();

    // Apply a colour-profile change as an Assign (changes interpretation, not
    // pixels — undoable), consistent with Edit > Assign Profile.
    const ColorProfile targetProfile = s.colorManaged
        ? ColorProfile::fromKind(s.colorProfileKind)
        : ColorProfile::invalid();
    const bool profileChanged = (s.colorManaged != m_doc->isColorManaged())
        || (s.colorManaged && !targetProfile.equivalentTo(m_doc->colorProfile()));
    if (profileChanged && m_ctrl) {
        auto cmd = std::make_unique<AssignProfileCommand>(m_doc, targetProfile);
        cmd->execute();
        m_ctrl->pushCommand(std::move(cmd));
        if (m_canvas)
            m_canvas->update();
    }

    refreshLayerPanel();
    refreshPropertiesPanel();
    updateStatusFromDocument();
    if (metadataChanged)
        markActiveTabDirty(true);
    updateDirtyUi();
}

void MainWindow::onAddLayer()
{
    if (!m_ctrl) return;

    if (!m_doc->size.isValid() || m_doc->size.isNull())
        m_doc->size = QSize(1024, 768);
    m_ctrl->newLayer();
    m_canvas->syncLayersToGpu();
    refreshPropertiesPanel();
}

void MainWindow::refreshLayersUi()
{
    if (m_canvas) { m_canvas->syncLayersToGpu(); m_canvas->update(); }
    refreshLayerPanel();
    refreshPropertiesPanel();
    updateLayerMenuState();
}

void MainWindow::groupSelectedLayers()
{
    if (!m_ctrl) return;
    m_ctrl->newGroup();   // newGroup() groups the current selection when present
    refreshLayersUi();
}

void MainWindow::ungroupSelectedLayers()
{
    if (!m_ctrl) return;
    m_ctrl->ungroupSelectedNodes();
    refreshLayersUi();
}

void MainWindow::arrangeActiveLayer(int kind)
{
    if (!m_ctrl || !m_doc) return;
    const int idx = m_ctrl->activeLayerIndex();
    const int flatCount = m_doc->flatCount();
    if (idx < 0) return;
    // Same flat-index reorder targets as the canvas context menu, for identical UX.
    switch (kind) {
    case 0: if (idx > 0)             m_ctrl->reorderNode(idx, idx - 1);   break; // forward (up)
    case 1: if (idx < flatCount - 1) m_ctrl->reorderNode(idx, idx + 2);   break; // backward (down)
    case 2: if (idx > 0)             m_ctrl->reorderNode(idx, 0);         break; // to front
    case 3: if (idx < flatCount - 1) m_ctrl->reorderNode(idx, flatCount); break; // to back
    default: return;
    }
    refreshLayersUi();
}

void MainWindow::toggleActiveClipping()
{
    if (!m_ctrl || !m_doc) return;
    auto* node = m_doc->activeNode();
    if (!node) return;

    // Two mechanisms share this one command, because the user is asking for one
    // thing ("clip this to the layer below") and the model stores it differently
    // per node type:
    //  - a Layer node carries a `clipped` flag and the compositors read the base
    //    layer's alpha straight off its sibling (LayerTreeNode::isClippedLayer);
    //  - an Adjustment node clips by BECOMING its host layer's child, which is
    //    Single Layer Mode and is handled below.
    if (node->type == LayerTreeNode::Type::Layer && node->layer) {
        std::vector<int> indices(m_doc->selectedFlatIndices.begin(),
                                 m_doc->selectedFlatIndices.end());
        if (indices.empty() && m_doc->activeFlatIndex >= 0)
            indices.push_back(m_doc->activeFlatIndex);
        m_ctrl->toggleClippingMask(indices);
        refreshLayersUi();
        return;
    }

    // "Single-Layer-Mode" here is a parent relationship: a clipped adjustment is
    // a child of its host Layer. The toggle moves the active adjustment between
    // the host's child list and the normal stack (both ops push an atomic
    // LayerTreeStateCommand).
    if (!node->isAdjustmentLayer()) return;
    const int adjIdx = m_doc->activeFlatIndex;
    const auto flat = m_doc->flatten();
    if (node->parent && node->parent->type == LayerTreeNode::Type::Layer) {
        // Release: re-insert into the stack just above the former host layer.
        int parentIdx = -1;
        for (int i = 0; i < static_cast<int>(flat.size()); ++i) {
            if (flat[i] == node->parent) { parentIdx = i; break; }
        }
        if (parentIdx >= 0)
            m_ctrl->moveAdjustmentToStack(adjIdx, parentIdx);
    } else {
        // Clip: attach to the first raster/text/shape Layer below it.
        int targetIdx = -1;
        for (int i = adjIdx + 1; i < static_cast<int>(flat.size()); ++i) {
            if (flat[i] && flat[i]->type == LayerTreeNode::Type::Layer && flat[i]->layer) {
                targetIdx = i;
                break;
            }
        }
        if (targetIdx >= 0)
            m_ctrl->moveAdjustmentToLayer(adjIdx, targetIdx);
    }
    refreshLayersUi();
}

void MainWindow::onRemoveLayer(int index)
{
    if (!m_ctrl) return;
    m_ctrl->removeNode(index);
    m_canvas->syncLayersToGpu();
    m_canvas->update();
    refreshPropertiesPanel();
}

void MainWindow::onActiveLayerChanged(int index)
{
    if (!m_doc || !m_canvas || !m_ctrl) return;

    if (index >= 0 && index < m_doc->flatCount()) {
        m_ctrl->setActiveNode(index);
        m_canvas->update();
        refreshPropertiesPanel();

        auto* node = m_doc->nodeAt(index);
        if (node && node->layer && node->layer->textData) {
            auto& td = *node->layer->textData;
            if (!td.spans.empty()) {
                auto& s = td.spans.back();
                QFont f(s.fontFamily);
                f.setPixelSize(static_cast<int>(s.fontSize));
                f.setBold(s.bold);
                f.setItalic(s.italic);
                m_toolBar->setTextFont(f);
                m_toolBar->setTextSize(static_cast<int>(s.fontSize));
                m_toolBar->setTextBold(s.bold);
                m_toolBar->setTextItalic(s.italic);
                m_toolBar->setTextUnderline(s.underline);
                m_toolBar->setTextStrikethrough(s.strikethrough);
                m_toolBar->setTextColor(s.color);
                m_toolBar->setTextTracking(s.letterSpacing);
            }
            m_toolBar->setTextLeading(td.lineSpacing);
            m_toolBar->setTextAlign(static_cast<int>(td.align));
            m_toolBar->setTool(static_cast<int>(CanvasView::Tool::Text));
            updateToolBarLayout();
        } else if (syncShapeOptionsBar(node)) {
            m_toolBar->setTool(static_cast<int>(CanvasView::Tool::Shape));
            updateToolBarLayout();
        }
    }
    updateDistortControls();
}

bool MainWindow::syncShapeOptionsBar(LayerTreeNode* node)
{
    if (!m_toolBar || !m_canvas || !m_doc) return false;
    if (!node || !node->layer || !node->layer->shapeData) return false;

    const ShapeData& sd = *node->layer->shapeData;
    const double docW = static_cast<double>(std::max(1, m_doc->size.width()));
    const int type = shapeToolTypeForPreset(sd.metadata.presetId);
    // strokeWidth and cornerRadius are stored normalised (px = normalised * docW
    // * 0.5); mirror the forward conversion used by the shape signal handlers.
    const double cornerRadiusNorm =
        sd.metadata.parameters.value(QStringLiteral("cornerRadius")).toDouble();
    const int strokePx = qRound(sd.style.strokeWidth * docW * 0.5);
    const int radiusPx = qRound(cornerRadiusNorm * docW * 0.5);
    int sides = 6;
    if (sd.metadata.presetId == QLatin1String("polygon"))
        sides = sd.metadata.parameters.value(QStringLiteral("sides"), 6).toInt();
    else if (sd.metadata.presetId == QLatin1String("star"))
        sides = sd.metadata.parameters.value(QStringLiteral("points"), 6).toInt();
    const float opacity = std::clamp(node->opacity, 0.0f, 1.0f);

    m_toolBar->setShapeType(type);
    m_toolBar->setShapeFillColor(sd.style.fillColor);
    m_toolBar->setShapeFillEnabled(sd.style.fillEnabled);
    m_toolBar->setShapeStrokeColor(sd.style.strokeColor);
    m_toolBar->setShapeStrokeEnabled(sd.style.strokeEnabled);
    m_toolBar->setShapeStrokeWidth(strokePx);
    m_toolBar->setShapeOpacity(qRound(opacity * 100.0f));
    m_toolBar->setShapeAntiAlias(sd.style.antiAlias);
    m_toolBar->setShapeCornerRadius(radiusPx);
    m_toolBar->setShapeSides(sides);

    // Sync the canvas shape-tool defaults so a subsequently drawn shape inherits
    // the selected layer's style.
    m_canvas->setShapeType(type);
    m_canvas->setShapeFillColor(sd.style.fillColor);
    m_canvas->setShapeFillEnabled(sd.style.fillEnabled);
    m_canvas->setShapeStrokeColor(sd.style.strokeColor);
    m_canvas->setShapeStrokeEnabled(sd.style.strokeEnabled);
    m_canvas->setShapeStrokeWidth(static_cast<float>(sd.style.strokeWidth));
    m_canvas->setShapeOpacity(opacity);
    m_canvas->setShapeAntiAlias(sd.style.antiAlias);
    m_canvas->setShapeCornerRadius(static_cast<float>(cornerRadiusNorm));
    m_canvas->setShapeSides(sides);
    return true;
}

void MainWindow::applyMoveOptionsPage()
{
    if (!m_toolBar) return;
    if (m_canvas && m_canvas->isFreeTransformActive()) {
        refreshTransformOptionsBar();
        m_toolBar->showTransformOptions(true);
        return;
    }
    // While the Move tool is active, the page follows the active layer type so
    // Text/Shape properties can be edited without switching tools.
    auto* node = m_doc ? m_doc->activeNode() : nullptr;
    auto* layer = node ? node->layer.get() : nullptr;
    if (layer && layer->isTextLayer())
        m_toolBar->setTool(static_cast<int>(CanvasView::Tool::Text));
    else if (layer && layer->isShapeLayer()) {
        syncShapeOptionsBar(node);
        m_toolBar->setTool(static_cast<int>(CanvasView::Tool::Shape));
    } else
        m_toolBar->setTool(static_cast<int>(CanvasView::Tool::Move));
}

void MainWindow::updateDistortControls()
{
    if (!m_toolBar) return;
    // Distort/Perspective live on the Skew tool's options page.
    const bool onSkew = m_canvas
        && m_canvas->currentTool() == CanvasView::Tool::Skew;
    const bool supported = onSkew && m_canvas
        && m_canvas->activeLayerSupportsDistort();
    m_toolBar->setDistortControlsEnabled(supported);
    int activeMode = -1;
    if (m_canvas && m_canvas->isDistortActive())
        activeMode = static_cast<int>(m_canvas->distortMode());
    m_toolBar->setDistortModeActive(activeMode);
    // Reset is available when the active layer actually carries a warp.
    m_toolBar->setDistortResetEnabled(supported && m_canvas
                                      && m_canvas->activeLayerHasDistort());
    // Apply commits the live session and exits edit mode.
    m_toolBar->setDistortApplyEnabled(supported && m_canvas
                                      && m_canvas->isDistortActive());
}

void MainWindow::updateLayerMenuState()
{
    bool hasDoc = (m_doc != nullptr);
    bool hasLayer = hasDoc && m_ctrl && m_ctrl->activeLayer() != nullptr;
    bool hasMultiple = hasDoc && m_doc->flatCount() >= 2;
    // Lock-derived gating mirrors the controller guards so the menu disables what
    // the operation would refuse anyway (locks evaluated only here, not deep in
    // the ops). See LayerTreeNode lock helpers / ImageController::checkDestructiveOp.
    auto* activeNode = (hasDoc && m_ctrl) ? m_doc->activeNode() : nullptr;
    bool isAdjustment = activeNode
        && activeNode->type == LayerTreeNode::Type::Adjustment;
    // Adjustment layers carry a (mask-only) Layer, but have no editable pixels.
    bool isRasterLayer = hasLayer && !isAdjustment
        && !m_ctrl->activeLayer()->isTextLayer()
        && !m_ctrl->activeLayer()->isShapeLayer();

    bool pixelsLocked   = activeNode && activeNode->isPixelEditingLocked();
    bool positionLocked = activeNode && activeNode->isPositionLocked();
    bool fullyLocked    = activeNode && activeNode->isFullyLocked();
    bool canEditPixels      = isRasterLayer && !pixelsLocked;
    bool canTransformPixels = canEditPixels && !positionLocked;
    // Flip H/V is a transform operation for any spatial layer/node, so it is
    // gated on Lock Position rather than Lock Image Pixels. Groups/multi flip
    // "as one area"; see ImageController::flipNodesAsUnit.
    bool isGroupNode = activeNode
        && activeNode->type == LayerTreeNode::Type::Group;
    bool multiSelected = hasDoc && m_doc->selectedFlatIndices.size() > 1;
    bool canFlipLayer = (hasLayer || isGroupNode || multiSelected)
        && !isAdjustment
        && !positionLocked;

    // Merge Down needs a real target: the next Layer sibling below the active
    // node in the SAME container (mergeDownTargetFlat never crosses a group
    // boundary and skips adjustment slots) — a flat-index heuristic gets both
    // of those wrong.
    bool hasMergeDownTarget = hasDoc && m_ctrl
                       && m_doc->activeFlatIndex >= 0
                       && m_ctrl->mergeDownTargetFlat(m_doc->activeFlatIndex) >= 0;
    bool hasMask = hasLayer && m_ctrl->hasLayerMask(m_ctrl->activeLayerIndex());

    m_addLayerAction->setEnabled(hasDoc);
    if (m_adjustmentLayerMenu) m_adjustmentLayerMenu->setEnabled(hasDoc);
    // Adjustment nodes (no pixels) can still be removed/duplicated, hence the
    // node-aware hasLayer test rather than isRasterLayer.
    bool hasNode = hasDoc && activeNode != nullptr;
    m_removeLayerAction->setEnabled((hasLayer || isAdjustment) && !fullyLocked);
    m_duplicateLayerAction->setEnabled(hasLayer || isAdjustment);
    Q_UNUSED(hasNode);
    m_fillLayerAction->setEnabled(canEditPixels);
    m_mergeVisibleAction->setEnabled(hasLayer && hasMultiple && !m_ctrl->anyFullyLockedVisibleLayer());
    m_mergeDownAction->setEnabled(hasMergeDownTarget && !pixelsLocked);
    m_mergeLayersAction->setEnabled(hasLayer && hasMultiple && !m_ctrl->anyFullyLockedVisibleLayer());
    m_flattenImageAction->setEnabled(hasDoc && m_ctrl && m_doc->flatCount() >= 1 && !m_ctrl->anyFullyLockedVisibleLayer());
    m_rasterizeLayerAction->setEnabled(hasLayer && !isAdjustment && !pixelsLocked);

    // Clipping Mask: the label states which of the two commands the current
    // selection would run, and the entry is disabled when neither applies (no
    // layer below to clip to, and nothing clipped to release).
    if (m_clippingMaskAction) {
        const int activeIdx = hasDoc ? m_doc->activeFlatIndex : -1;
        const bool clipped  = m_ctrl && m_ctrl->isClippingMask(activeIdx);
        const bool isBase   = m_ctrl && m_ctrl->isClippingMaskBase(activeIdx);
        const bool canClip  = m_ctrl && m_ctrl->canCreateClippingMask(activeIdx);
        // An adjustment keeps the Single-Layer-Mode meaning of this command, so
        // it stays enabled for one whenever a host layer exists below it.
        const bool adjustmentClip = isAdjustment && activeNode
            && (activeNode->parent
                    ? activeNode->parent->type == LayerTreeNode::Type::Layer
                    : hasMultiple);
        m_clippingMaskAction->setText((clipped || isBase)
                                          ? tr("Release &Clipping Mask")
                                          : tr("Create &Clipping Mask"));
        m_clippingMaskAction->setEnabled(clipped || isBase || canClip || adjustmentClip);
    }

    // Smart Objects. Per-item gating, not one "is this a Smart Object?" switch:
    // it is what tells the user WHY something is unavailable — Embed Linked
    // greys out on an embedded instance, not on every Smart Object.
    if (m_smartObjectMenu) {
        const auto smartData = (hasDoc && m_ctrl)
            ? m_ctrl->smartObjectDataAt(m_doc->activeFlatIndex) : nullptr;
        const bool isSmartObject = smartData != nullptr;
        const bool isLinked = isSmartObject
            && smartData->mode == SmartObjectStorageMode::Linked;
        const bool isEmbedded = isSmartObject && !isLinked;

        m_smartObjectMenu->setEnabled(hasDoc);
        // Converting a Smart Object into another one is legal — that is how
        // you nest them.
        m_smartObjectConvertAction->setEnabled(
            (hasLayer || isAdjustment || isGroupNode || multiSelected) && !fullyLocked);
        m_smartObjectViaCopyAction->setEnabled(isSmartObject);
        m_smartObjectEditAction->setEnabled(isSmartObject);
        m_smartObjectReplaceAction->setEnabled(isSmartObject && !pixelsLocked);
        // Relink / Update / Embed Linked are Linked-only: embedded content has
        // no file to re-point at and can never be out of date, because editing
        // it invalidates its instances immediately.
        m_smartObjectRelinkAction->setEnabled(isLinked && !pixelsLocked);
        m_smartObjectUpdateAction->setEnabled(isLinked);
        m_smartObjectToEmbeddedAction->setEnabled(isLinked && !pixelsLocked);
        m_smartObjectToLinkedAction->setEnabled(isEmbedded && !pixelsLocked);
    }
    // Place is a File-menu action: it needs an open document and nothing else.
    if (m_smartObjectPlaceEmbeddedAction)
        m_smartObjectPlaceEmbeddedAction->setEnabled(hasDoc);
    if (m_smartObjectPlaceLinkedAction)
        m_smartObjectPlaceLinkedAction->setEnabled(hasDoc);
    if (m_layerStylesAction) m_layerStylesAction->setEnabled(hasLayer && !isAdjustment && !fullyLocked);
    m_layerMaskAddAction->setEnabled(hasLayer && !hasMask && !fullyLocked);
    m_layerMaskRemoveAction->setEnabled(hasLayer && hasMask && !fullyLocked);
    m_layerMaskApplyAction->setEnabled(hasLayer && hasMask && !pixelsLocked);
    m_layerMaskToggleAction->setEnabled(hasLayer && hasMask);
    m_layerMaskFromSelAction->setEnabled(hasLayer && !fullyLocked && m_doc && m_doc->selection.active() && !m_doc->selection.isEmpty());
    m_layerMaskInvertAction->setEnabled(hasLayer && hasMask && !fullyLocked);
    m_layerMaskCopyAction->setEnabled(hasLayer && hasMask);
    m_layerMaskPasteAction->setEnabled(hasLayer && !fullyLocked && m_ctrl && m_ctrl->hasCopiedMask());
    m_layerMaskClearAction->setEnabled(hasLayer && hasMask && !fullyLocked);
    if (hasLayer && hasMask)
        m_layerMaskToggleAction->setText(m_ctrl->isLayerMaskEnabled(m_ctrl->activeLayerIndex())
            ? tr("&Disable Mask") : tr("&Enable Mask"));
    m_flipHorizontalAction->setEnabled(canFlipLayer);
    m_flipVerticalAction->setEnabled(canFlipLayer);
    m_resizeLayerAction->setEnabled(canTransformPixels);

    if (m_colorAdjAction)   m_colorAdjAction->setEnabled(canEditPixels);
    if (m_gaussianAction)   m_gaussianAction->setEnabled(canEditPixels);
    if (m_medianAction)     m_medianAction->setEnabled(canEditPixels);
    if (m_boxBlurAction)        m_boxBlurAction->setEnabled(canEditPixels);
    if (m_bilateralBlurAction)  m_bilateralBlurAction->setEnabled(canEditPixels);
    if (m_motionBlurAction)     m_motionBlurAction->setEnabled(canEditPixels);
    if (m_radialBlurAction)     m_radialBlurAction->setEnabled(canEditPixels);
    if (m_zoomBlurAction)       m_zoomBlurAction->setEnabled(canEditPixels);
    if (m_sharpenAction)    m_sharpenAction->setEnabled(canEditPixels);
    if (m_posterizeAction)  m_posterizeAction->setEnabled(canEditPixels);
    if (m_thresholdAction)  m_thresholdAction->setEnabled(canEditPixels);
    if (m_edgesAction)      m_edgesAction->setEnabled(canEditPixels);
    if (m_grayscaleAction)  m_grayscaleAction->setEnabled(canEditPixels);
    if (m_invertAction)     m_invertAction->setEnabled(canEditPixels);
    if (m_noiseAction)      m_noiseAction->setEnabled(canEditPixels);
    if (m_removeBgAction)   m_removeBgAction->setEnabled(canEditPixels);
    if (m_aiUpscaleLayerAction) m_aiUpscaleLayerAction->setEnabled(isRasterLayer);
    if (m_aiUpscaleDocumentAction) m_aiUpscaleDocumentAction->setEnabled(hasDoc);
}

void MainWindow::updateAlignBarState()
{
    if (!m_alignBar) return;

    // Align operates on any transformable entity: a single layer, a single
    // Group (using its aggregate bounds), or a multi-selection (treated as one
    // composite entity). Only a hard position lock disables it.
    bool hasAlignableEntity = false;
    if (m_doc) {
        if (m_doc->selectedFlatIndices.size() > 1) {
            hasAlignableEntity = true;
        } else if (auto* node = m_doc->activeNode()) {
            hasAlignableEntity = node->canTransform()
                && (node->type == LayerTreeNode::Type::Layer
                    || node->type == LayerTreeNode::Type::Group);
        }
    }

    // The Align bar is a shared "auxiliary page" of the options bar shown to the
    // right of the current tool page. Its visibility is governed by its own rule —
    // independent of (and never owned by) any single tool: it appears only when a
    // layout-bearing tool is active (Move / Shape / Text) AND a compatible entity
    // is selected (Group / Text / Shape / Pixel layer, or a multi-selection). This
    // keeps the rule in one place rather than scattered across the individual tools.
    const bool toolAllowsAlign = m_canvas
        && (m_canvas->currentTool() == CanvasView::Tool::Move
            || m_canvas->currentTool() == CanvasView::Tool::Shape
            || m_canvas->currentTool() == CanvasView::Tool::Text);
    const bool showAlignPage = toolAllowsAlign && hasAlignableEntity;

    if (m_toolBar && m_toolBar->setAuxiliaryOptionsVisible(showAlignPage)) {
        // The auxiliary options content changed: refresh the options bar geometry.
        updateToolBarLayout();
    }
    // When shown the entity is alignable by construction, so keep the buttons live.
    m_alignBar->updateButtons(showAlignPage);
}

void MainWindow::onToolSelected(int tool)
{
    // AI Object Selection may only activate when AI is enabled and a model is
    // installed. When blocked, the guard shows an alert, keeps the current tool
    // selected and we return without switching or starting any job (spec §12).
    if (tool == static_cast<int>(CanvasView::Tool::AiSelect)
        && !guardAiObjectSelectionActivation())
        return;
    if (tool == static_cast<int>(CanvasView::Tool::AiRemove)) {
        // Temporarily hidden: AI Remove Tool will be released later.
        // Do not remove the implementation. Only the user-facing UI is disabled.
        if (m_canvas)
            m_canvas->setTool(CanvasView::Tool::Move);
        if (m_toolsPanel)
            m_toolsPanel->setActiveTool(static_cast<int>(CanvasView::Tool::Move));
        if (m_toolBar)
            applyMoveOptionsPage();
        return;
    }

    clearSelectRefinePreview();
    if (m_canvas)
        m_canvas->setTool(static_cast<CanvasView::Tool>(tool));
    else if (m_toolBar)
        m_toolBar->setTool(tool);

    if (m_toolBar) {
        // Point the AI options page at the active canvas's controller when the
        // AI Object Selection tool is chosen.
        if (tool == static_cast<int>(CanvasView::Tool::AiSelect) && m_canvas) {
            m_toolBar->setAiToolPageMode(AiToolPageMode::ObjectSelection);
            m_toolBar->bindAiController(m_canvas->aiSelectionController());
            ensureAiControllerConnected();
        }
        // Temporarily hidden: AI Remove Tool will be released later.
        // Do not remove the implementation. Only the user-facing UI is disabled.
        /*
        else if (tool == static_cast<int>(CanvasView::Tool::AiRemove) && m_canvas) {
            m_toolBar->setAiToolPageMode(AiToolPageMode::RemoveObject);
            m_toolBar->bindAiRemoveController(m_canvas->aiRemoveController());
        }
        */
        m_toolBar->setVisible(true);
    }
    updateToolBarLayout();
    updateDistortControls();
}

bool MainWindow::guardAiObjectSelectionActivation()
{
    auto* rt = AiRuntimeManager::instance();
    AiAlertAction action = AiAlertAction::Cancel;
    bool blocked = true;

    if (!rt->isRuntimeAvailable()) {
        action = AiAlertService::showAiRuntimeError(
            this, AiCompatibilityManager::instance()->runtimeCompatibility().messages.value(0));
    } else if (!rt->isAiEnabled()) {
        action = AiAlertService::showAiDisabled(this);
    } else if (!AiModelCompatibilityChecker::hasRequiredModel(QStringLiteral("object_selection"))
               && !AiModelCompatibilityChecker::hasRequiredModel(QStringLiteral("segmentation"))) {
        action = AiAlertService::showMissingModel(this, QStringLiteral("object_selection"));
    } else {
        blocked = false;
    }

    if (!blocked)
        return true;

    if (action == AiAlertAction::OpenSettings)
        onOpenAiSettings();
    // Keep the previously-active tool selected — never switch to AI Select.
    if (m_toolsPanel && m_canvas)
        m_toolsPanel->setActiveTool(static_cast<int>(m_canvas->currentTool()));
    return false;
}

bool MainWindow::guardAiRemoveActivation()
{
    auto* rt = AiRuntimeManager::instance();
    AiAlertAction action = AiAlertAction::Cancel;
    bool blocked = true;

    if (!rt->isRuntimeAvailable()) {
        action = AiAlertService::showAiRuntimeError(
            this, AiCompatibilityManager::instance()->runtimeCompatibility().messages.value(0));
    } else if (!rt->isAiEnabled()) {
        action = AiAlertService::showAiDisabled(this);
    } else if (AiModelRegistry::instance()->installedModelsForTask(QStringLiteral("remove_object")).isEmpty()) {
        action = AiAlertService::showMissingModel(this, QStringLiteral("remove_object"));
    } else {
        blocked = false;
    }

    if (!blocked)
        return true;

    if (action == AiAlertAction::OpenSettings)
        onOpenAiSettings();
    if (m_toolsPanel && m_canvas)
        m_toolsPanel->setActiveTool(static_cast<int>(m_canvas->currentTool()));
    return false;
}

void MainWindow::ensureAiControllerConnected()
{
    if (!m_canvas)
        return;
    if (auto* ai = m_canvas->aiSelectionController()) {
        connect(ai, &AiObjectSelectionController::aiAlertRequested,
                this, &MainWindow::onAiAlertRequested, Qt::UniqueConnection);
        connect(ai, &AiObjectSelectionController::busyChanged,
                this, &MainWindow::onAiSelectBusyChanged, Qt::UniqueConnection);
        connect(ai, &AiObjectSelectionController::statusChanged,
                this, &MainWindow::onAiSelectStatusChanged, Qt::UniqueConnection);
    }
}

void MainWindow::switchAiProviderToCpu()
{
    auto* rt = AiRuntimeManager::instance();
    AiRuntimeSettings s = rt->settings();
    s.executionProvider = QString::fromLatin1(AiProvider::kCpu);
    rt->setSettings(s);
    if (m_canvas) {
        if (auto* ai = m_canvas->aiSelectionController())
            ai->onProviderOrSettingsChanged();
    }
}

void MainWindow::onAiAlertRequested(const AiCompatibilityMessage& message)
{
    AiAlertAction action;
    if (message.actionId == QLatin1String("cuda_oom"))
        action = AiAlertService::showCudaOom(this, /*fallbackDisabled=*/true);
    else if (message.actionId == QLatin1String("provider_incompatible"))
        action = AiAlertService::showProviderIncompatible(this, message);
    else
        action = AiAlertService::showAiError(this, message);

    if (action == AiAlertAction::OpenSettings)
        onOpenAiSettings();
    else if (action == AiAlertAction::UseCpu)
        switchAiProviderToCpu();
}

// Maps the raw progress labels emitted by the AI pipeline (untranslated literals)
// to user-facing, translatable strings. Unknown labels pass through unchanged.
static QString aiSelectStatusText(const QString& raw)
{
    static const QHash<QString, QString> kMap = {
        { QStringLiteral("Loading model"),        MainWindow::tr("Loading AI model…") },
        { QStringLiteral("Loading refine model"), MainWindow::tr("Loading AI model…") },
        { QStringLiteral("Building embedding"),   MainWindow::tr("Analyzing image…") },
        { QStringLiteral("Preparing image"),      MainWindow::tr("Preparing image…") },
        { QStringLiteral("Selecting object"),     MainWindow::tr("Selecting object…") },
        { QStringLiteral("Selecting subject"),    MainWindow::tr("Selecting subject…") },
        { QStringLiteral("Refining mask"),        MainWindow::tr("Refining mask…") },
        { QStringLiteral("Removing background"),  MainWindow::tr("Removing background…") },
    };
    const auto it = kMap.constFind(raw);
    return it == kMap.constEnd() ? raw : it.value();
}

void MainWindow::onAiSelectStatusChanged(const QString& status)
{
    // Mirror the controller's progress onto the app status bar (the options bar no
    // longer shows it) and keep the loading dialog message in sync if it is up.
    // While a job is running, keep the message pinned (no auto-clear) so a slow
    // stage doesn't blank the status bar mid-job; terminal messages auto-clear.
    m_aiLoadingMessage = aiSelectStatusText(status);
    showViewportStatusMessage(m_aiLoadingMessage, m_aiSelectBusy ? 0 : 4000);
    if (m_aiLoadingDialog && m_aiLoadingDialog->isVisible())
        m_aiLoadingDialog->setMessage(m_aiLoadingMessage);
}

void MainWindow::onAiSelectBusyChanged(bool busy)
{
    m_aiSelectBusy = busy;

    if (busy) {
        // The job runs async; show the loading dialog only if it is still going
        // after 2s so quick operations don't flash a dialog.
        if (!m_aiLoadingTimer) {
            m_aiLoadingTimer = new QTimer(this);
            m_aiLoadingTimer->setSingleShot(true);
            connect(m_aiLoadingTimer, &QTimer::timeout,
                    this, &MainWindow::showAiLoadingDialogIfStillBusy);
        }
        m_aiLoadingTimer->start(2000);
        return;
    }

    // Done (or cancelled): drop the pending timer and hide the dialog.
    if (m_aiLoadingTimer)
        m_aiLoadingTimer->stop();
    if (m_aiLoadingDialog)
        m_aiLoadingDialog->hide();
}

void MainWindow::showAiLoadingDialogIfStillBusy()
{
    if (!m_aiSelectBusy)
        return;

    if (!m_aiLoadingDialog) {
        m_aiLoadingDialog = new ProgressDialog(this);
        connect(m_aiLoadingDialog, &ProgressDialog::cancelRequested, this, [this]() {
            if (m_canvas)
                if (auto* ai = m_canvas->aiSelectionController())
                    ai->cancelPending();
        });
    }

    m_aiLoadingDialog->setWindowTitle(tr("AI Select"));
    m_aiLoadingDialog->setMessage(m_aiLoadingMessage.isEmpty()
                                      ? tr("Working…") : m_aiLoadingMessage);
    m_aiLoadingDialog->setCancelable(true);
    m_aiLoadingDialog->setProgressValue(-1);   // indeterminate
    m_aiLoadingDialog->show();
    m_aiLoadingDialog->raise();
    m_aiLoadingDialog->activateWindow();
}

void MainWindow::onBrushSizeChanged(int size)
{
    if (m_canvas)
        m_canvas->setBrushSize(static_cast<float>(size));
    if (m_brushPanel)
        m_brushPanel->setSize(static_cast<float>(size));
}

void MainWindow::onBrushOpacityChanged(int opacity)
{
    if (m_canvas)
        m_canvas->setBrushOpacity(static_cast<float>(opacity) / 100.0f);
}

void MainWindow::onBrushHardnessChanged(int hardness)
{
    if (m_canvas)
        m_canvas->setBrushHardness(static_cast<float>(hardness) / 100.0f);
    if (m_brushPanel)
        m_brushPanel->setHardness(static_cast<float>(hardness) / 100.0f);
}

void MainWindow::onBrushFlowChanged(int flow)
{
    if (m_canvas)
        m_canvas->setBrushFlow(static_cast<float>(flow) / 100.0f);
}

void MainWindow::onBrushColorChanged(const QColor& color)
{
    if (m_colorEngine) {
        m_colorEngine->setForegroundColor(color);
    } else if (m_canvas) {
        m_canvas->setBrushColor(color);
    }
}

void MainWindow::showBrushPanel()
{
    if (!m_brushPanelDock) return;

    // First open: present it floating near the centre of the window.
    // Subsequent opens keep wherever the user docked/moved it.
    if (!m_brushPanelFirstShown) {
        m_brushPanelFirstShown = true;
        m_brushPanelDock->setFloating(true);
        const QSize sz(360, 620);
        m_brushPanelDock->resize(sz);
        const QRect host = geometry();
        m_brushPanelDock->move(host.center() - QPoint(sz.width() / 2, sz.height() / 2));
    }

    m_brushPanelDock->show();
    m_brushPanelDock->raise();
    m_brushPanelDock->activateWindow();
}

void MainWindow::openBrushImportDialog(const QStringList& files)
{
    BrushPresetManager* presets = m_toolBar ? m_toolBar->presetManager() : nullptr;
    if (!presets) return;

    if (!m_brushImportManager) {
        m_brushImportManager = new BrushImportManager();
        m_brushImportManager->registerDefaultAdapters();
    }

    auto* dlg = new BrushImportDialog(m_brushImportManager, presets, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &BrushImportDialog::brushesImported, this,
            [this](const QStringList& names, const QStringList& groups) {
                Q_UNUSED(groups);
                // The preset library already refreshed every brush view via
                // BrushPresetManager::presetsChanged. Surface the new brushes:
                // warm previews, reveal the panel and select the first import.
                if (m_brushPanel) {
                    m_brushPanel->warmCacheAsync();
                    if (!names.isEmpty()) {
                        m_brushPanel->revealPreset(names.first());
                        if (BrushPresetManager* presets = m_toolBar ? m_toolBar->presetManager() : nullptr) {
                            if (BrushPreset* preset = presets->findPreset(names.first()))
                                applyBrushPreset(*preset);
                        }
                    }
                }
                showBrushPanel();
            });
    if (!files.isEmpty())
        dlg->addFiles(files);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void MainWindow::showCustomShapesPanel()
{
    if (!m_customShapesPanelDock) return;

    if (!m_customShapesPanelFirstShown) {
        m_customShapesPanelFirstShown = true;
        m_customShapesPanelDock->setFloating(true);
        const QSize sz(380, 520);
        m_customShapesPanelDock->resize(sz);
        if (m_toolBar && m_toolBar->customShapesButton()) {
            const QPoint pos = m_toolBar->customShapesButton()->mapToGlobal(
                QPoint(0, m_toolBar->customShapesButton()->height() + 4));
            m_customShapesPanelDock->move(pos);
        } else {
            const QRect host = geometry();
            m_customShapesPanelDock->move(host.center() - QPoint(sz.width() / 2, sz.height() / 2));
        }
    }

    m_customShapesPanelDock->show();
    m_customShapesPanelDock->raise();
    m_customShapesPanelDock->activateWindow();
}

void MainWindow::showBrushSettingsPanel()
{
    if (!m_brushSettingsDock) return;
    m_brushSettingsDock->setFloating(true);
    m_brushSettingsDock->show();
    m_brushSettingsDock->raise();
    m_brushSettingsDock->activateWindow();
}

void MainWindow::childEvent(QChildEvent* e)
{
    QMainWindow::childEvent(e);
    // A dock group window being added/removed as a child is the only reliable
    // notification that the dock layout changed in a way that can leave a stray
    // single-dock container (the lone remaining dock emits no QDockWidget
    // signal). Schedule a maintenance pass; it defers past any active drag.
    // Restrict to widget children: non-widget children (e.g. the internal
    // singleShot timer objects) would otherwise reschedule us in a tight loop.
    // if (e && (e->added() || e->removed()) && e->child() && e->child()->isWidgetType())
    //     scheduleDockMaintenance();
}

void MainWindow::scheduleDockMaintenance()
{
    return;
    // Coalesce the burst of signals one drag/close emits into a single pass.
    if (m_dockMaintenanceScheduled || m_inDockMaintenance) return;
    m_dockMaintenanceScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        m_dockMaintenanceScheduled = false;
        dissolveSingleDockGroups();
    });
}

void MainWindow::dissolveSingleDockGroups()
{
    if (m_inDockMaintenance) return;

    // Reorganising dock containers while Qt is mid-drag (a mouse button is held)
    // is exactly what crashed before — wait until the drag settles and retry.
    if (QApplication::mouseButtons() != Qt::NoButton) {
        if (!m_dockMaintenanceScheduled) {
            m_dockMaintenanceScheduled = true;
            QTimer::singleShot(100, this, [this]() {
                m_dockMaintenanceScheduled = false;
                dissolveSingleDockGroups();
            });
        }
        return;
    }

    m_inDockMaintenance = true;

    // ── DIAGNOSTIC: list EVERY QDockWidgetGroupWindow with its dock count, the
    // dock titles, and whether it is visible. Critical question to answer from
    // the log: does a normally-floated single dock (e.g. Brushes) ALSO show up
    // as a 1-dock group (i.e. floating is always wrapped under GroupedDragging),
    // or only the split-leftover one?
    int groupCount = 0;
    for (QWidget* w : findChildren<QWidget*>()) {
        if (qstrcmp(w->metaObject()->className(), "QDockWidgetGroupWindow") != 0)
            continue;
        ++groupCount;
        const QList<QDockWidget*> docks = w->findChildren<QDockWidget*>();
        QStringList titles;
        for (QDockWidget* d : docks)
            titles << (d->windowTitle() + (d->isHidden() ? "(hidden)" : ""));
    }

    // When a tab is dragged out of a floating tab group, Qt can leave the lone
    // remaining dock wrapped in a QDockWidgetGroupWindow container (a stray
    // white-bordered frame instead of a plain floating dock). Unwrap any 1-dock
    // group: detach the dock into a normal standalone floating window, which
    // empties the container so Qt drops it. QPointer + re-validate for safety.
    QList<QPointer<QWidget>> singles;
    for (QWidget* w : findChildren<QWidget*>()) {
        if (qstrcmp(w->metaObject()->className(), "QDockWidgetGroupWindow") == 0 &&
            w->findChildren<QDockWidget*>().size() == 1)
            singles.append(w);
    }
    for (const QPointer<QWidget>& wp : singles) {
        QWidget* w = wp.data();
        if (!w) continue;
        const QList<QDockWidget*> docks = w->findChildren<QDockWidget*>();
        if (docks.size() != 1) continue; // state changed since collection
        QDockWidget* d = docks.front();
        const QRect geom = w->geometry();
        const bool wasVisible = !d->isHidden();
        addDockWidget(Qt::RightDockWidgetArea, d); // detach from the group window
        d->setFloating(true);                      // → plain standalone floater
        if (geom.isValid())
            d->setGeometry(geom);                  // keep it where the group was
        if (wasVisible)
            d->show();
    }

    m_inDockMaintenance = false;
}

void MainWindow::resetDockLayout()
{
    if (!m_layersDock) return; // docks not built yet

    // A reset is a change to the *editing* layout, but on the Home tab every
    // editing dock is hidden and showing them below would spill panels over
    // the Home page. Step out of Home mode, lay the default out, then step
    // back in: re-entering snapshots the fresh arrangement for the next
    // document tab and hides the docks again, all within one event loop
    // iteration so nothing is ever painted in between.
    const bool homeActive = m_homeDockMode;
    if (homeActive)
        setHomeDockMode(false);

    // Pull every right-side panel back into the right dock area, docked and
    // visible, so the regrouping below starts from a known state regardless of
    // where the user dragged / floated / closed them. Histogram is hidden again
    // after tabification so it only appears when opened from Window.
    const QList<QDockWidget*> panels = {
        m_histogramDock, m_propsDock, m_colorDock, m_swatchesDock,
        m_historyDock, m_layersDock, m_channelsDock, m_agentDock
    };
    for (QDockWidget* d : panels) {
        addDockWidget(Qt::RightDockWidgetArea, d);
        d->setFloating(false);
        d->show();
    }

    // Default arrangement: smaller TOP group
    // [Properties | Color | Swatches | History] over the larger
    // BOTTOM group [Layers | AI Agent]. Split first, then tabify into each cell.
    // Histogram is kept in this tab group but starts hidden.
    splitDockWidget(m_histogramDock, m_layersDock, Qt::Vertical);
    tabifyDockWidget(m_histogramDock, m_propsDock);
    tabifyDockWidget(m_propsDock, m_colorDock);
    tabifyDockWidget(m_colorDock, m_swatchesDock);
    tabifyDockWidget(m_swatchesDock, m_historyDock);
    // Channels sits with Layers: they are the two views of the same document
    // and the user switches between them constantly.
    tabifyDockWidget(m_layersDock, m_channelsDock);
    tabifyDockWidget(m_layersDock, m_agentDock);

    resizeDocks({m_histogramDock, m_layersDock}, {3, 7}, Qt::Vertical);

    // Force the right column's default width down to its minimum; otherwise
    // Qt's dock layout sizes it from the panels' sizeHint(), which ignores
    // setMinimumWidth() above and renders wider than intended.
    const int minW = QGuiApplication::primaryScreen()->availableGeometry().width() / 8;
    resizeDocks({m_layersDock}, {minW}, Qt::Horizontal);

    m_propsDock->raise(); // default active tab per group
    m_layersDock->raise();
    m_histogramDock->hide();

    QTimer::singleShot(0, this, [this]() {
        refreshDockTitleBars();

        // tabifyDockWidget() creates new internal QTabBar instances that are
        // not re-polished automatically, so all stylesheet rules are ignored.
        // Force a full re-polish on every tab bar child.
        for (QTabBar* tb : findChildren<QTabBar*>()) {
            tb->style()->unpolish(tb);
            tb->style()->polish(tb);
        }

        const QRect available = screen()->availableGeometry();
        if (!available.contains(frameGeometry()))
            setGeometry(geometry().intersected(available));
    });

    // On-demand floating panels go back to their default hidden+floating state.
    for (QDockWidget* d : { m_brushSettingsDock, m_brushPanelDock, m_customShapesPanelDock, m_generativeFillDock }) {
        if (!d) continue;
        addDockWidget(Qt::RightDockWidgetArea, d);
        d->setFloating(true);
        d->hide();
    }

    if (homeActive)
        setHomeDockMode(true);
}

void MainWindow::applyBrushPressureEnabled(bool on)
{
    if (m_canvas)
        m_canvas->setBrushPressureEnabled(on);
    if (m_toolBar)
        m_toolBar->setBrushPressureEnabled(on);     // non-emitting
    if (m_brushDynamics)
        m_brushDynamics->setPressureEnabled(on);    // non-emitting
    if (m_brushPressureAction) {
        QSignalBlocker b(m_brushPressureAction);
        m_brushPressureAction->setChecked(on);
    }
    showViewportStatusMessage(on ? tr("Pen Pressure: On") : tr("Pen Pressure: Off"), 1500);
}

void MainWindow::applyBrushPreset(const BrushPreset& preset, bool switchToBrushTool)
{
    const auto& s = preset.settings;
    // Picking a brush implies wanting to paint: switch to the Brush tool unless
    // the current tool already consumes brush settings itself (Eraser, Clone
    // Stamp/Healing), in which case selecting a preset shouldn't steal the tool.
    // The startup default-brush load opts out so it doesn't force the initial tool.
    if (switchToBrushTool && m_canvas) {
        const auto tool = m_canvas->currentTool();
        const bool usesBrushSettings = tool == CanvasView::Tool::Brush
            || tool == CanvasView::Tool::Eraser
            || tool == CanvasView::Tool::CloneStamp;
        if (!usesBrushSettings)
            m_canvas->setTool(CanvasView::Tool::Brush);
    }
    if (m_canvas) {
        m_canvas->setBrushSize(s.size);
        m_canvas->setBrushHardness(s.hardness);
        m_canvas->setBrushOpacity(s.opacity);
        m_canvas->setBrushFlow(s.flow);
        m_canvas->setBrushType(s.type);
        m_canvas->setBrushApplication(s.application);
        m_canvas->setBrushPaintMode(s.paintMode);
        if (s.tipSource == BrushTipSource::Image) {
            QImage tip;
            if (!s.tipImageData.isEmpty())
                tip.loadFromData(QByteArray::fromBase64(s.tipImageData.toUtf8()));
            else if (!s.tipImagePath.isEmpty())
                tip.load(s.tipImagePath);
            if (!tip.isNull())
                m_canvas->setBrushTipImage(tip);
            else
                m_canvas->setBrushTipSource(BrushTipSource::Circle);
        } else {
            m_canvas->setBrushTipSource(BrushTipSource::Circle);
        }
        m_canvas->setBrushAngle(s.angle);
        m_canvas->setBrushRoundness(s.roundness);
        m_canvas->setBrushFlipX(s.flipX);
        m_canvas->setBrushFlipY(s.flipY);
        m_canvas->setBrushScatter(s.scatterOption, s.scatter);
        m_canvas->setBrushColorDynamics(s.colorDynamics);
        m_canvas->setBrushTextureConfig(s.textureConfig);
        m_canvas->setBrushDualBrushConfig(s.dualBrushConfig);
        m_canvas->setBrushBlendMode(s.blendMode);
        m_canvas->setBrushSmoothingMode(s.smoothingMode);
        m_canvas->setBrushSmoothingRadius(s.smoothingRadius);
        m_canvas->setBrushSpacing(s.spacing);
        m_canvas->setBrushAirbrush(s.airbrush);
        m_canvas->setBrushAirbrushRate(s.airbrushRate);
        m_canvas->setBrushWetEdges(s.wetEdges);
        m_canvas->setBrushAutoBrushConfig(s.autoBrush);
        m_canvas->setBrushAutoSpacing(s.useAutoSpacing, s.autoSpacingCoeff);
        m_canvas->setBrushTipRemap(s.tipBrightness, s.tipContrast, s.tipMidpoint);
        m_canvas->setBrushSizeOption(s.sizeOption);
        m_canvas->setBrushOpacityOption(s.opacityOption);
        m_canvas->setBrushFlowOption(s.flowOption);
        m_canvas->setBrushRotationOption(s.rotationOption);
        m_canvas->setBrushRatioOption(s.ratioOption);
    }
    const bool pressureOn = brushPressureEnabled(s);
    if (m_toolBar) {
        m_toolBar->setBrushSize(static_cast<int>(s.size));
        m_toolBar->setBrushHardness(static_cast<int>(s.hardness * 100.0f));
        m_toolBar->setBrushOpacity(static_cast<int>(s.opacity * 100.0f));
        m_toolBar->setBrushFlow(static_cast<int>(s.flow * 100.0f));
        m_toolBar->setBrushPressureEnabled(pressureOn);
        m_toolBar->setCurrentBrushPreset(preset);
    }
    if (m_brushPressureAction) {
        QSignalBlocker b(m_brushPressureAction);
        m_brushPressureAction->setChecked(pressureOn);
    }
    if (m_brushDynamics) {
        m_brushDynamics->setFromSettings(s);
        m_brushDynamics->setCurrentPreset(preset.name);
    }
    // Highlight the applied preset in the Brushes panel. This function is reached
    // both from the panel's own selection and from the startup default-brush load,
    // so it's the right single place to keep the panel selection in sync.
    if (m_brushPanel)
        m_brushPanel->setCurrentPreset(preset.name);
}

void MainWindow::onTextFontChanged(const QFont& font)
{
    if (m_canvas)
        m_canvas->setTextFont(font);
}

void MainWindow::onTextSizeChanged(int size)
{
    if (m_canvas)
        m_canvas->setTextSize(size);
}

void MainWindow::onTextBoldChanged(bool bold)
{
    if (m_canvas)
        m_canvas->setTextBold(bold);
}

void MainWindow::onTextItalicChanged(bool italic)
{
    if (m_canvas)
        m_canvas->setTextItalic(italic);
}

void MainWindow::onTextUnderlineChanged(bool underline)
{
    if (m_canvas)
        m_canvas->setTextUnderline(underline);
}

void MainWindow::onTextStrikethroughChanged(bool strikethrough)
{
    if (m_canvas)
        m_canvas->setTextStrikethrough(strikethrough);
}

void MainWindow::onTextColorChanged(const QColor& color)
{
    // The text colour control is an explicit instruction to recolour the
    // selected text, so it applies whatever the active tool is — unlike the
    // foreground swatch, which only recolours text under the Move tool (see the
    // ColorEngine::foregroundColorChanged handler). Routing this through the
    // foreground alone used to swallow the change for every other tool, the Text
    // tool itself included.
    if (m_canvas)
        m_canvas->setTextColor(color);
    // Keep the foreground in sync: it is what the next text layer is created with.
    if (m_colorEngine)
        m_colorEngine->setForegroundColor(color);
}

void MainWindow::onTextAlignChanged(int align)
{
    if (m_canvas)
        m_canvas->setTextAlign(align);
}

void MainWindow::onTextTrackingChanged(double tracking)
{
    if (m_canvas)
        m_canvas->setTextTracking(tracking);
}

void MainWindow::onTextLeadingChanged(double leading)
{
    if (m_canvas)
        m_canvas->setTextLeading(leading);
}

void MainWindow::onUndo()
{
    clearSelectRefinePreview();
    if (m_ctrl && m_ctrl->history().canUndo()) {
        m_ctrl->undo();
        m_canvas->syncLayersToGpu();
        m_canvas->update();
        refreshLayerPanel();
        refreshPropertiesPanel();
        updateLayerMenuState();
    }
}

void MainWindow::onRedo()
{
    clearSelectRefinePreview();
    if (m_ctrl && m_ctrl->history().canRedo()) {
        m_ctrl->redo();
        m_canvas->syncLayersToGpu();
        m_canvas->update();
        refreshLayerPanel();
        refreshPropertiesPanel();
        updateLayerMenuState();
    }
}

void MainWindow::openRefineEdgeDialog()
{
    if (!m_ctrl || !m_ctrl->document()) return;
    auto* doc = m_ctrl->document();
    if (!doc->selection.active() || doc->selection.isEmpty()) return;

    // Refine against the document composite: the selection mask is doc-sized,
    // while layer images may be transformed, tiled, or stale.
    QImage srcImage = compositeImage(doc);
    if (srcImage.isNull())
        srcImage = QImage(doc->size, QImage::Format_RGBA8888);

    RefineEdgeDialog dlg(doc->selection.image(), srcImage, this);
    if (dlg.exec() == QDialog::Accepted) {
        QImage before = doc->selection.image().copy();
        bool beforeActive = doc->selection.active();
        doc->selection.image() = dlg.resultMask().copy();
        doc->selection.setActive(true);
        m_ctrl->history().push(std::make_unique<SelectionCommand>(
            doc, before, doc->selection.image().copy(),
            beforeActive, true, "refine_edge"));
        emit m_ctrl->selectionChanged();
        emit m_ctrl->imageChanged();
    }
}

void MainWindow::clearSelectRefinePreview()
{
    m_selectRefineBaseMask = QImage();
    m_selectRefineBaseActive = false;
    m_selectRefineOp = SelectRefineOp::None;
}

void MainWindow::applySelectRefinePreview(SelectRefineOp op, int value)
{
    if (!m_doc || !m_ctrl || op == SelectRefineOp::None)
        return;
    if (!m_doc->selection.active() || m_doc->selection.isEmpty())
        return;

    if (m_selectRefineBaseMask.isNull()
        || m_selectRefineOp != op
        || m_selectRefineBaseMask.size() != m_doc->selection.image().size()) {
        m_selectRefineBaseMask = m_doc->selection.image().copy();
        m_selectRefineBaseActive = m_doc->selection.active();
        m_selectRefineOp = op;
    }

    QImage before = m_doc->selection.image().copy();
    bool beforeActive = m_doc->selection.active();

    m_doc->selection.image() = m_selectRefineBaseMask.copy();
    m_doc->selection.setActive(m_selectRefineBaseActive);

    const char* opName = "select_refine";
    switch (op) {
        case SelectRefineOp::Feather: m_doc->selection.feather(static_cast<float>(value)); opName = "select_feather"; break;
        case SelectRefineOp::Grow:    m_doc->selection.grow(value); opName = "select_grow"; break;
        case SelectRefineOp::Shrink:  m_doc->selection.shrink(value); opName = "select_shrink"; break;
        case SelectRefineOp::Border:  m_doc->selection.border(value); opName = "select_border"; break;
        case SelectRefineOp::Smooth:  m_doc->selection.smooth(static_cast<float>(value)); opName = "select_smooth"; break;
        case SelectRefineOp::None: break;
    }
    m_doc->selection.setActive(!m_doc->selection.isEmpty());

    // The agent path (select_feather etc.) is undoable; the toolbar path must
    // be too, or these refines are permanent.
    QImage after = m_doc->selection.image().copy();
    bool afterActive = m_doc->selection.active();
    if (after != before || afterActive != beforeActive) {
        m_ctrl->history().push(std::make_unique<SelectionCommand>(
            m_doc, before, std::move(after),
            beforeActive, afterActive, opName));
    }

    // Re-upload the ants texture without dropping the refine base we just set.
    m_inSelectRefine = true;
    emit m_ctrl->selectionChanged();
    m_inSelectRefine = false;
    emit m_ctrl->imageChanged();
}

void MainWindow::onZoomChanged(float zoom)
{
    if (!m_zoomCombo)
        return;

    m_zoomCombo->setEnabled(m_canvas != nullptr);
    m_zoomCombo->setZoom(zoom);
}

void MainWindow::onMouseCoordChanged(QPointF coord)
{
    m_coordLabel->setText(tr("X: %1  Y: %2")
        .arg(static_cast<int>(coord.x()))
        .arg(static_cast<int>(coord.y())));
}

void MainWindow::updateStatusFromDocument()
{
    if (!m_doc) {
        if (m_zoomCombo) {
            m_zoomCombo->setZoom(1.0f);
            m_zoomCombo->setEnabled(false);
        }
        m_sizeLabel->setText(tr("Size: —"));
        if (m_colorProfileLabel)
            m_colorProfileLabel->setText(tr("Color: —"));
        if (m_proofColorsAction) {
            QSignalBlocker block(m_proofColorsAction);
            m_proofColorsAction->setChecked(false);
            m_proofColorsAction->setEnabled(false);
        }
        return;
    }
    if (m_zoomCombo)
        m_zoomCombo->setEnabled(true);
    m_sizeLabel->setText(tr("Size: %1×%2")
        .arg(m_doc->size.width())
        .arg(m_doc->size.height()));
    if (m_colorProfileLabel) {
        QString profileText = m_doc->isColorManaged()
            ? m_doc->colorProfile().displayName()
            : tr("Untagged");
        if (m_doc->profileSource() == ColorProfileSource::AssumedByPolicy)
            profileText = tr("%1 (assumed)").arg(profileText);
        QString labelText = tr("Color: %1").arg(profileText);
        QString tip = profileText;
        if (m_doc->softProofEnabled()) {
            const QString proofName = m_doc->softProofSettings().proofProfile.displayName();
            labelText += tr("  •  Proof: %1").arg(proofName);
            tip += tr("\nSoft proofing: %1").arg(proofName);
        }
        m_colorProfileLabel->setText(labelText);
        m_colorProfileLabel->setToolTip(tip);
    }
    // Keep the Proof Colors check state in sync when switching documents/tabs.
    if (m_proofColorsAction) {
        QSignalBlocker block(m_proofColorsAction);
        m_proofColorsAction->setChecked(m_doc->softProofEnabled());
        m_proofColorsAction->setEnabled(true);
    }
    onZoomChanged(m_doc->zoom);
}

void MainWindow::refreshLayerPanel()
{
    if (m_layerPanel)
        m_layerPanel->refresh();
}

void MainWindow::syncChannelsPanel()
{
    if (!m_channelsPanel)
        return;
    // Canvas first: the panel reads the composite through it, and a refresh
    // triggered by setController would otherwise build its thumbnails against
    // the previous tab's canvas.
    m_channelsPanel->setCanvas(m_canvas);
    m_channelsPanel->setController(m_ctrl);
}

// ── Channels commands that span documents ──────────────────────
//
// The Channels panel owns everything that happens inside one document; these
// four need more than that — a new tab, another tab's document, or a dialog
// listing the open documents — which only MainWindow can provide. They still
// end in ImageController, so the undo entry and the data model are the same
// ones the panel's own commands produce.

namespace {

// Display label for a document in the channel dialogs' document combos.
QString documentComboLabel(const QString& name, bool dirty)
{
    return dirty ? name + QStringLiteral(" *") : name;
}

std::vector<QString> channelLabelsFor(const Document* doc)
{
    std::vector<QString> labels;
    if (!doc)
        return labels;
    for (const auto& ch : doc->channels)
        labels.push_back(ch.name);
    return labels;
}

} // namespace

std::vector<QString> MainWindow::openDocumentLabels() const
{
    std::vector<QString> labels;
    for (const auto& tab : m_tabs)
        labels.push_back(documentComboLabel(tab.name, tab.isDirty));
    return labels;
}

void MainWindow::splitChannelsIntoDocuments()
{
    if (!m_ctrl || !m_doc)
        return;
    // Splitting consumes a flattened image, so it is only offered for a
    // document whose channels are the colour components alone; anything else
    // would silently drop the extra channels.
    if (!m_doc->channels.empty()) {
        AppMessageDialog::warning(this, tr("Split Channels"),
            tr("Split Channels works on the colour channels only. Delete or "
               "merge the document's alpha and spot channels first."));
        return;
    }
    const std::vector<QImage> planes = m_ctrl->splitColorChannels();
    if (planes.empty()) {
        AppMessageDialog::warning(this, tr("Split Channels"),
            tr("This document has nothing to split."));
        return;
    }

    static const char* kComponentSuffix[3] = {"Red", "Green", "Blue"};
    const QString baseName = QFileInfo(m_doc->name).completeBaseName().isEmpty()
        ? m_doc->name : QFileInfo(m_doc->name).completeBaseName();

    for (int i = 0; i < static_cast<int>(planes.size()); ++i) {
        const QImage& plane = planes[static_cast<size_t>(i)];
        const QString name = QStringLiteral("%1.%2").arg(baseName)
            .arg(tr(kComponentSuffix[std::min(i, 2)]));
        auto& tab = createTab(name, plane.size());
        if (!tab.document)
            continue;
        tab.document->colorMode = QStringLiteral("Grayscale");

        // One flattened layer per component, which is what a split produces:
        // the separation is an image in its own right, not a channel of one.
        auto node = std::make_unique<LayerTreeNode>();
        node->type = LayerTreeNode::Type::Layer;
        node->name = tr("Background");
        node->layer = std::make_shared<Layer>();
        node->layer->name = node->name;
        node->layer->cpuImage = plane.convertToFormat(QImage::Format_RGBA8888);
        node->layer->owner = node.get();
        node->layer->textureOutdated = true;
        tab.document->roots.push_back(std::move(node));
        tab.document->activeFlatIndex = 0;
        tab.document->selectedFlatIndices = {0};
        ++tab.document->compositionGeneration;
        tab.isDirty = true;
        if (tab.canvas) {
            tab.canvas->syncLayersToGpu();
            tab.canvas->update();
        }
    }
    updateDirtyUi();
    refreshLayerPanel();
    syncChannelsPanel();
}

void MainWindow::mergeChannelsFromDocuments()
{
    if (m_tabs.size() < 3) {
        AppMessageDialog::warning(this, tr("Merge Channels"),
            tr("Merging channels needs at least three open grayscale documents, "
               "one per colour component."));
        return;
    }

    MergeChannelsDialog dialog(openDocumentLabels(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const std::vector<int> sources = dialog.componentSources();
    std::vector<QImage> planes;
    QSize size;
    for (int index : sources) {
        if (index < 0 || index >= static_cast<int>(m_tabs.size()))
            return;
        auto* doc = m_tabs[static_cast<size_t>(index)].document.get();
        auto* ctrl = m_tabs[static_cast<size_t>(index)].controller.get();
        if (!doc || !ctrl)
            return;
        const std::vector<QImage> split = ctrl->splitColorChannels();
        if (split.empty())
            return;
        // A grayscale source has identical components, so taking the first is
        // the same as taking its luminance — and it keeps a colour document
        // from contributing only part of itself without saying so.
        if (size.isValid() && split.front().size() != size) {
            AppMessageDialog::warning(this, tr("Merge Channels"),
                tr("The selected documents must all have the same pixel "
                   "dimensions."));
            return;
        }
        size = split.front().size();
        planes.push_back(split.front());
    }

    const QImage merged = ImageController::mergeColorChannels(planes);
    if (merged.isNull()) {
        AppMessageDialog::warning(this, tr("Merge Channels"),
            tr("The selected documents could not be merged."));
        return;
    }

    auto& tab = createTab(tr("Merged"), merged.size());
    if (!tab.document)
        return;
    auto node = std::make_unique<LayerTreeNode>();
    node->type = LayerTreeNode::Type::Layer;
    node->name = tr("Background");
    node->layer = std::make_shared<Layer>();
    node->layer->name = node->name;
    node->layer->cpuImage = merged;
    node->layer->owner = node.get();
    node->layer->textureOutdated = true;
    tab.document->roots.push_back(std::move(node));
    tab.document->activeFlatIndex = 0;
    tab.document->selectedFlatIndices = {0};
    ++tab.document->compositionGeneration;
    tab.isDirty = true;
    if (tab.canvas) {
        tab.canvas->syncLayersToGpu();
        tab.canvas->update();
    }
    updateDirtyUi();
    refreshLayerPanel();
    syncChannelsPanel();
}

void MainWindow::duplicateChannelWithDialog(int channelIndex)
{
    if (!m_ctrl || !m_doc || channelIndex < 0
        || channelIndex >= static_cast<int>(m_doc->channels.size()))
        return;

    const DocumentChannel& source = m_doc->channels[static_cast<size_t>(channelIndex)];
    DuplicateChannelDialog dialog(
        m_doc->uniqueChannelName(source.kind) , openDocumentLabels(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const int destination = dialog.destinationIndex();
    if (destination < 0) {
        // "New": a fresh grayscale document holding the channel's coverage, so
        // the duplicate is editable on its own instead of landing nowhere.
        QImage plane = m_doc->channelCoverageMask(channelIndex);
        if (plane.isNull())
            return;
        if (dialog.invertContents()) {
            for (int y = 0; y < plane.height(); ++y) {
                uchar* row = plane.scanLine(y);
                for (int x = 0; x < plane.width(); ++x)
                    row[x] = static_cast<uchar>(255 - row[x]);
            }
        }
        auto& tab = createTab(dialog.channelName().isEmpty() ? source.name
                                                             : dialog.channelName(),
                              plane.size());
        if (!tab.document)
            return;
        tab.document->colorMode = QStringLiteral("Grayscale");
        auto node = std::make_unique<LayerTreeNode>();
        node->type = LayerTreeNode::Type::Layer;
        node->name = tr("Background");
        node->layer = std::make_shared<Layer>();
        node->layer->name = node->name;
        node->layer->cpuImage = plane.convertToFormat(QImage::Format_RGBA8888);
        node->layer->owner = node.get();
        node->layer->textureOutdated = true;
        tab.document->roots.push_back(std::move(node));
        tab.document->activeFlatIndex = 0;
        tab.document->selectedFlatIndices = {0};
        ++tab.document->compositionGeneration;
        tab.isDirty = true;
        if (tab.canvas) {
            tab.canvas->syncLayersToGpu();
            tab.canvas->update();
        }
        updateDirtyUi();
        return;
    }

    Document* target = m_doc;
    int targetTab = m_activeTabIndex;
    if (destination < static_cast<int>(m_tabs.size())) {
        target = m_tabs[static_cast<size_t>(destination)].document.get();
        targetTab = destination;
    }
    const int created = m_ctrl->duplicateChannel(channelIndex, dialog.channelName(),
                                                 dialog.invertContents(), target);
    if (created >= 0 && target != m_doc && targetTab >= 0
        && targetTab < static_cast<int>(m_tabs.size())) {
        // The destination document changed, so ITS tab is the dirty one.
        m_tabs[static_cast<size_t>(targetTab)].isDirty = true;
        updateDirtyUi();
    }
    syncChannelsPanel();
}

void MainWindow::showSaveSelectionDialog()
{
    if (!m_ctrl || !m_doc)
        return;
    if (!m_doc->selection.active() || m_doc->selection.isEmpty()) {
        AppMessageDialog::warning(this, tr("Save Selection"),
            tr("There is no active selection to save."));
        return;
    }

    SaveSelectionDialog dialog(openDocumentLabels(),
                               channelLabelsFor(m_doc),
                               m_doc->uniqueChannelName(ChannelKind::Alpha), this);
    // Only documents with the same pixel dimensions can receive the mask; the
    // combo lists them all and the operation validates on accept.
    connect(&dialog, &SaveSelectionDialog::destinationDocumentChanged,
            this, [this, &dialog](int index) {
        const Document* target = (index >= 0 && index < static_cast<int>(m_tabs.size()))
            ? m_tabs[static_cast<size_t>(index)].document.get() : nullptr;
        dialog.setChannelLabels(channelLabelsFor(target));
    });
    if (dialog.exec() != QDialog::Accepted)
        return;

    const int docIndex = dialog.destinationDocumentIndex();
    Document* target = (docIndex >= 0 && docIndex < static_cast<int>(m_tabs.size()))
        ? m_tabs[static_cast<size_t>(docIndex)].document.get() : m_doc;
    ImageController* targetCtrl = (docIndex >= 0
                                   && docIndex < static_cast<int>(m_tabs.size()))
        ? m_tabs[static_cast<size_t>(docIndex)].controller.get() : m_ctrl;
    if (!target || !targetCtrl)
        return;
    if (target->size != m_doc->size) {
        AppMessageDialog::warning(this, tr("Save Selection"),
            tr("The destination document must have the same pixel dimensions."));
        return;
    }

    const int channelIndex = dialog.destinationChannelIndex();
    if (target == m_doc) {
        if (channelIndex < 0)
            m_ctrl->saveSelectionAsChannel(dialog.channelName());
        else
            m_ctrl->saveSelectionIntoChannel(channelIndex, dialog.combineMode());
    } else {
        // Across documents the mask is carried over by hand: the destination's
        // own controller owns the change and its own history records it.
        DocumentChannel ch;
        ch.kind = ChannelKind::Alpha;
        ch.name = dialog.channelName().isEmpty()
            ? target->uniqueChannelName(ChannelKind::Alpha) : dialog.channelName();
        ch.data = m_doc->selection.image()
            .convertToFormat(QImage::Format_Grayscale8).copy();
        if (channelIndex < 0) {
            target->addChannel(std::move(ch));
        } else {
            // Combining into an existing channel of another document reuses the
            // destination controller so the rule (and the history entry) is the
            // destination's.
            const QImage saved = target->selection.image();
            const bool savedActive = target->selection.active();
            target->selection.resize(target->size.width(), target->size.height());
            target->selection.image() = ch.data;
            target->selection.setActive(true);
            targetCtrl->saveSelectionIntoChannel(channelIndex, dialog.combineMode());
            target->selection.image() = saved;
            target->selection.setActive(savedActive);
        }
        if (docIndex >= 0 && docIndex < static_cast<int>(m_tabs.size()))
            m_tabs[static_cast<size_t>(docIndex)].isDirty = true;
        updateDirtyUi();
    }
    syncChannelsPanel();
}

void MainWindow::showLoadSelectionDialog()
{
    if (!m_ctrl || !m_doc)
        return;

    LoadSelectionDialog dialog(openDocumentLabels(),
                               channelLabelsFor(m_doc),
                               m_doc->selection.active() && !m_doc->selection.isEmpty(),
                               this);
    connect(&dialog, &LoadSelectionDialog::sourceDocumentChanged,
            this, [this, &dialog](int index) {
        const Document* source = (index >= 0 && index < static_cast<int>(m_tabs.size()))
            ? m_tabs[static_cast<size_t>(index)].document.get() : nullptr;
        dialog.setChannelLabels(channelLabelsFor(source));
    });
    if (dialog.exec() != QDialog::Accepted)
        return;

    const int docIndex = dialog.sourceDocumentIndex();
    const int channelIndex = dialog.sourceChannelIndex();
    if (channelIndex < 0) {
        AppMessageDialog::warning(this, tr("Load Selection"),
            tr("The selected document has no channel to load."));
        return;
    }
    Document* source = (docIndex >= 0 && docIndex < static_cast<int>(m_tabs.size()))
        ? m_tabs[static_cast<size_t>(docIndex)].document.get() : m_doc;
    if (!source)
        return;

    if (source == m_doc) {
        m_ctrl->loadChannelAsSelection(channelIndex, dialog.combineMode(),
                                       dialog.invert());
    } else {
        if (source->size != m_doc->size) {
            AppMessageDialog::warning(this, tr("Load Selection"),
                tr("The source document must have the same pixel dimensions."));
            return;
        }
        // Copy the channel in, then load it from here, so the result lands in
        // THIS document's selection and history.
        DocumentChannel ch = source->channels[static_cast<size_t>(channelIndex)];
        ch.id = 0;
        ch.visible = false;
        ch.data = ch.data.copy();
        const int temp = m_doc->addChannel(std::move(ch));
        if (temp < 0)
            return;
        m_ctrl->loadChannelAsSelection(temp, dialog.combineMode(), dialog.invert());
        m_doc->deleteChannel(temp);
    }
    syncChannelsPanel();
    if (m_canvas)
        m_canvas->update();
}

namespace {
// W/H/X/Y/rotation a layer's Transform panel should display, derived from the
// node's VISUAL frame (the same oriented box the canvas draws via
// TransformController::cornersFromNode) — never node->transform directly.
//
// This matters for Shape layers: committing a free transform bakes the rotation
// into the shape geometry and rebuilds node->transform AXIS-ALIGNED around the
// new rotated-AABB raster (see ImageController::bakeShapeLayerResolutionInPlace).
// Reading node->transform there reports rotation 0 and the bounding-box size, so
// the fields jumped on commit. visualFrameForNode reconstructs the oriented box
// from the geometry, so the values are invariant across the bake and match what
// the user saw mid-gesture.
struct LayerTransformMetrics {
    double wPx = 0.0;
    double hPx = 0.0;
    double xPx = 0.0;
    double yPx = 0.0;
    double rotDeg = 0.0;
};

LayerTransformMetrics computeLayerTransformMetrics(const LayerTreeNode* node,
                                                   const QSize& docSize)
{
    const QTransform frame = TransformController::visualFrameForNode(node, docSize);
    const double docW = std::max(1, docSize.width());
    const double docH = std::max(1, docSize.height());

    float hw, hh, rotRad;
    QPointF center;
    TransformController::decompose(frame, hw, hh, center, rotRad);

    // Axis-aligned bounding box of the visual frame, in canvas NDC.
    const QPolygonF corners = TransformController::cornersFromTransform(frame);
    double minXn = 1e9, maxYn = -1e9;
    for (const QPointF& p : corners) {
        minXn = std::min(minXn, p.x());
        maxYn = std::max(maxYn, p.y());
    }

    // Rotation in the aspect-corrected VISUAL space (metric = document pixel
    // dimensions), the same space the canvas drag-rotate uses. A raw-NDC angle
    // disagrees with what's on screen on non-square documents.
    float vhw, vhh, vrot;
    QPointF vcenter;
    TransformController::decomposeVisual(frame, QPointF(docW, docH),
                                         QSize(1, 1), vhw, vhh, vcenter, vrot);

    LayerTransformMetrics m;
    m.wPx = std::abs(hw) * docW;
    m.hPx = std::abs(hh) * docH;
    m.xPx = (minXn + 1.0) * docW * 0.5;        // left edge in px
    m.yPx = (1.0 - maxYn) * docH * 0.5;        // top edge in px (y-down)
    m.rotDeg = vrot * 180.0 / M_PI;
    return m;
}
} // namespace

void MainWindow::refreshPropertiesPanel()
{
    // Each tab owns its ImageController, so the adjustment editors must always
    // be bound to the active document's controller. Re-point here (idempotent
    // when unchanged) so switching document/tab/layer never leaves them reading
    // a background document's nodes.
    if (m_propsPanel)
        m_propsPanel->setController(m_ctrl);

    if (!m_doc) {
        m_propsPanel->clear();
        return;
    }
    // No layer selected (empty document, deselected, or a non-layer node such as
    // a group) → show the Document/Canvas page instead of an empty panel.
    auto* node = (m_doc->flatCount() > 0) ? m_doc->activeNode() : nullptr;
    if (!node || !node->layer) {
        refreshPropertiesDocumentPage();
        return;
    }

    // Adjustment layers show their dedicated editor instead of the default
    // layer-properties view.
    if (node->isAdjustmentLayer()
        && node->adjustment->type == QLatin1String("curves")) {
        m_propsPanel->showCurvesEditor(m_doc->activeFlatIndex);
        return;
    }
    if (node->isAdjustmentLayer()
        && node->adjustment->type == QLatin1String("colorbalance")) {
        m_propsPanel->showColorBalanceEditor(m_doc->activeFlatIndex);
        return;
    }
    if (node->isAdjustmentLayer()
        && node->adjustment->type == QLatin1String("huesaturation")) {
        m_propsPanel->showHueSaturationEditor(m_doc->activeFlatIndex);
        return;
    }
    if (node->isAdjustmentLayer()
        && node->adjustment->type == QLatin1String("solidcolor")) {
        m_propsPanel->showSolidColorEditor(m_doc->activeFlatIndex);
        return;
    }
    m_propsPanel->showLayerProperties();

    // ── Transform interface: derive W/H/X/Y/rotation in document pixels from the
    // layer's transform (the panel never owns transform math — it only displays
    // and forwards edits back through applyPropertiesTransformEdit). ──
    PropertiesPanel::LayerKind kind = PropertiesPanel::LayerKind::Pixel;
    if (node->layer->isTextLayer())
        kind = PropertiesPanel::LayerKind::Text;
    else if (node->layer->isShapeLayer())
        kind = PropertiesPanel::LayerKind::Shape;

    const LayerTransformMetrics m = computeLayerTransformMetrics(node, m_doc->size);

    m_propsPanel->setLayerTransformInfo(kind, m.wPx, m.hPx, m.xPx, m.yPx, m.rotDeg,
                                        node->canTransform() && !node->isPositionLocked());

    PropertiesPanel::SmartObjectInfo smartInfo;
    if (const auto data = m_ctrl ? m_ctrl->smartObjectDataAt(m_doc->activeFlatIndex)
                                 : nullptr) {
        smartInfo.valid = true;
        smartInfo.linked = data->mode == SmartObjectStorageMode::Linked;
        smartInfo.linkBroken = data->linkBroken;
        smartInfo.path = m_ctrl->smartObjectLinkedPathAt(m_doc->activeFlatIndex);
        smartInfo.sourceSize = data->state.sourceSize;
        smartInfo.sourceDpi = data->state.sourceDpi;
        smartInfo.instanceCount = m_ctrl->smartObjectInstanceCountAt(m_doc->activeFlatIndex);
        if (auto* container = m_ctrl->documentContainer()) {
            if (const auto referenced = container->document(data->docId))
                smartInfo.documentName = referenced->name;
        }
        if (smartInfo.documentName.isEmpty()) {
            smartInfo.documentName = smartInfo.linked
                ? QFileInfo(data->linkedPath).completeBaseName() : node->name;
        }
    }
    m_propsPanel->setSmartObjectInfo(smartInfo);

    bool hasMask = node->layer && !node->layer->maskImage.isNull();
    m_propsPanel->setMaskInfo(hasMask, node->layer->maskDensity, node->layer->maskFeather);
    if (hasMask && m_ctrl)
        m_propsPanel->setMaskOverlayState(m_ctrl->isMaskOverlayVisible(),
                                          m_ctrl->maskOverlayOpacity());
}

void MainWindow::refreshTransformOptionsBar()
{
    if (!m_toolBar)
        return;

    const bool active = m_canvas && m_canvas->isFreeTransformActive();
    auto* node = (m_doc && m_doc->flatCount() > 0) ? m_doc->activeNode() : nullptr;
    if (!active || !node || !node->canTransform() || !m_doc) {
        m_toolBar->setTransformFieldsEnabled(false);
        return;
    }

    const LayerTransformMetrics m = computeLayerTransformMetrics(node, m_doc->size);

    m_toolBar->setTransformValues(m.wPx, m.hPx, m.xPx, m.yPx, m.rotDeg);
    m_toolBar->setTransformFieldsEnabled(true);
}

void MainWindow::applyPropertiesTransformEdit(int field, double value)
{
    if (!m_doc || !m_ctrl || !m_canvas || m_doc->activeFlatIndex < 0) return;
    auto* node = m_doc->activeNode();
    if (!node || !node->canTransform()) return;

    const double docW = std::max(1, m_doc->size.width());
    const double docH = std::max(1, m_doc->size.height());

    // Edit the node's VISUAL frame (the on-screen oriented box that the panel
    // displayed via computeLayerTransformMetrics), not node->transform directly.
    // For Shape layers the commit-time bake stores rotation in the geometry and
    // leaves node->transform axis-aligned, so editing node->transform would
    // disagree with the shown W/H/X/Y/rotation. The resulting new frame is
    // converted back to a local transform via the world-delta model below.
    const QTransform frame =
        TransformController::visualFrameForNode(node, m_doc->size);

    float hw, hh, rotRad;
    QPointF center;
    TransformController::decompose(frame, hw, hh, center, rotRad);

    // AABB top-left (canvas NDC) of an arbitrary transform frame.
    auto aabbTopLeft = [](const QTransform& t, double& minX, double& maxY) {
        const QPolygonF c = TransformController::cornersFromTransform(t);
        minX = 1e9; maxY = -1e9;
        for (const QPointF& p : c) {
            minX = std::min(minX, p.x());
            maxY = std::max(maxY, p.y());
        }
    };

    double curMinX, curMaxY;
    aabbTopLeft(frame, curMinX, curMaxY);
    const double curWpx = std::abs(hw) * docW;
    const double curHpx = std::abs(hh) * docH;

    QTransform newFrame = frame;
    switch (field) {
    case PropertiesPanel::FieldWidth:
    case PropertiesPanel::FieldHeight: {
        double newHw = hw, newHh = hh;
        if (field == PropertiesPanel::FieldWidth) {
            const double newWpx = std::max(1.0, value);
            newHw = (hw < 0 ? -1.0 : 1.0) * newWpx / docW;
            const bool linked = (m_canvas->isFreeTransformActive() && m_toolBar)
                ? m_toolBar->transformProportionsLocked()
                : (m_propsPanel && m_propsPanel->proportionsLocked());
            if (linked && curWpx > 1e-6)
                newHh = hh * (newWpx / curWpx);
        } else {
            const double newHpx = std::max(1.0, value);
            newHh = (hh < 0 ? -1.0 : 1.0) * newHpx / docH;
            const bool linked = (m_canvas->isFreeTransformActive() && m_toolBar)
                ? m_toolBar->transformProportionsLocked()
                : (m_propsPanel && m_propsPanel->proportionsLocked());
            if (linked && curHpx > 1e-6)
                newHw = hw * (newHpx / curHpx);
        }
        // Anchor the box top-left so resizing doesn't shift the reported X/Y.
        const QTransform tentative =
            TransformController::compose(newHw, newHh, center, rotRad);
        double newMinX, newMaxY;
        aabbTopLeft(tentative, newMinX, newMaxY);
        const QPointF anchored(center.x() + (curMinX - newMinX),
                               center.y() + (curMaxY - newMaxY));
        newFrame = TransformController::compose(newHw, newHh, anchored, rotRad);
        break;
    }
    case PropertiesPanel::FieldX: {
        const double curLeftPx = (curMinX + 1.0) * docW * 0.5;
        const double dNdcX = 2.0 * (value - curLeftPx) / docW;
        newFrame = TransformController::compose(hw, hh,
                    QPointF(center.x() + dNdcX, center.y()), rotRad);
        break;
    }
    case PropertiesPanel::FieldY: {
        const double curTopPx = (1.0 - curMaxY) * docH * 0.5;
        const double dNdcY = -2.0 * (value - curTopPx) / docH;
        newFrame = TransformController::compose(hw, hh,
                    QPointF(center.x(), center.y() + dNdcY), rotRad);
        break;
    }
    case PropertiesPanel::FieldRotation: {
        // The frame lives in canvas NDC, which is anisotropic on a non-square
        // document; composing a rotation there (compose) shears the layer — the
        // bug this fixes. Rebuild the frame in the aspect-corrected VISUAL space
        // (metric = document pixel dimensions), exactly like the canvas
        // drag-rotate, so the result is a clean visual rotation.
        const QPointF halfExtents(docW, docH);
        const QSize vp(1, 1);
        float vhw, vhh, vrot;
        QPointF vcenter;
        TransformController::decomposeVisual(frame, halfExtents, vp,
                                             vhw, vhh, vcenter, vrot);
        const double newRot = value * M_PI / 180.0;
        newFrame = TransformController::composeVisual(vhw, vhh, vcenter,
                                                  static_cast<float>(newRot),
                                                  halfExtents, vp);
        break;
    }
    default:
        return;
    }

    // Convert the edited visual frame back into a local node->transform via the
    // world-delta model (local' = local · P · M · P⁻¹, with M = frame⁻¹·newFrame
    // the canvas-NDC delta and P the parent's accumulated transform). This keeps
    // grouped/nested layers correct, and for a full-base ungrouped layer it
    // reduces to node->transform = newFrame (there frame == node->transform).
    QTransform parentAccum;
    for (auto* p = node->parent; p; p = p->parent)
        parentAccum = parentAccum * p->transform;

    bool ok = false;
    const QTransform frameInv = frame.inverted(&ok);
    if (!ok) return;
    const QTransform worldDelta = frameInv * newFrame;
    const QTransform parentInv = parentAccum.inverted(&ok);
    if (!ok) return;
    const QTransform newLocal = node->transform * parentAccum * worldDelta * parentInv;

    // Routed through the canvas so per-type baking (text font size, shape
    // re-render) and undo grouping match a manual transform.
    m_canvas->applyTransformFromPanel(newLocal);
}

void MainWindow::refreshPropertiesDocumentPage()
{
    if (!m_propsPanel || !m_doc)
        return;
    const int w = std::max(1, m_doc->size.width());
    const int h = std::max(1, m_doc->size.height());
    // Highlight the orientation matching the current aspect (neutral when square).
    m_propsPanel->setDocumentInfo(w, h,
                                  /*portraitActive*/ h > w,
                                  /*landscapeActive*/ w > h);
    const RulerGuideSettings rg = RulerGuideSettings::load();
    m_propsPanel->setRulerGuideState(rg.rulers.showRulers,
                                     static_cast<int>(rg.rulers.unit));
    m_propsPanel->showDocumentProperties();
}

void MainWindow::applyCanvasSizeEdit(int field, double value)
{
    if (!m_ctrl || !m_doc)
        return;

    const int oldW = std::max(1, m_doc->size.width());
    const int oldH = std::max(1, m_doc->size.height());
    const bool linked = m_propsPanel && m_propsPanel->canvasProportionsLocked();

    int newW = oldW;
    int newH = oldH;
    if (field == PropertiesPanel::FieldWidth) {
        newW = std::max(1, static_cast<int>(std::lround(value)));
        if (linked)
            newH = std::max(1, static_cast<int>(std::lround(
                static_cast<double>(oldH) * newW / oldW)));
    } else if (field == PropertiesPanel::FieldHeight) {
        newH = std::max(1, static_cast<int>(std::lround(value)));
        if (linked)
            newW = std::max(1, static_cast<int>(std::lround(
                static_cast<double>(oldW) * newH / oldH)));
    } else {
        return;  // X/Y have no canvas offset concept.
    }

    const QSize target(newW, newH);
    if (target == m_doc->size) {
        // No change requested — restore the displayed value to the real size.
        refreshPropertiesDocumentPage();
        return;
    }

    // This is a Canvas control: change the canvas frame without resampling the
    // content (crop/pad around the centre), exactly like the orientation buttons.
    // The link button only decides the proportional target — never the resize type.
    CanvasResizeOptions options;
    options.targetSize = target;
    options.anchor = CanvasAnchor::Center;
    options.fillExtension = false;

    const qint64 area = static_cast<qint64>(oldW) * oldH;
    if (area >= m_doc->perfConfig.autoTileMinArea)
        m_ctrl->resizeCanvasAsync(options);
    else
        m_ctrl->resizeCanvas(options);
}

void MainWindow::applyCanvasOrientation(bool portrait)
{
    if (!m_ctrl || !m_doc)
        return;

    const int w = std::max(1, m_doc->size.width());
    const int h = std::max(1, m_doc->size.height());

    // Only swap when the current orientation differs from the requested one.
    const bool needsSwap = portrait ? (w > h) : (h > w);
    if (!needsSwap) {
        refreshPropertiesDocumentPage();  // re-sync the active-button highlight
        return;
    }

    // Swap W/H via the Canvas Size path: no resample, no rotation, no distortion —
    // the content keeps its scale, the canvas frame just changes shape.
    CanvasResizeOptions options;
    options.targetSize = QSize(h, w);
    options.anchor = CanvasAnchor::Center;
    options.fillExtension = false;

    const qint64 area = static_cast<qint64>(w) * h;
    if (area >= m_doc->perfConfig.autoTileMinArea)
        m_ctrl->resizeCanvasAsync(options);
    else
        m_ctrl->resizeCanvas(options);
}

void MainWindow::refreshHistoryPanel()
{
    if (!m_ctrl) {
        m_historyPanel->clear();
        return;
    }
    m_historyPanel->refresh(
        m_ctrl->historyStateNames(),
        m_ctrl->history().currentIndex());
}

void MainWindow::resizeEvent(QResizeEvent* e)
{
    if (m_titleBar)
        m_titleBar->resize(e->size().width(), TitleBar::kBarHeight);
    QMainWindow::resizeEvent(e);
    updateToolBarLayout();
}

void MainWindow::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::WindowStateChange && m_titleBar)
        m_titleBar->updateMaximizeButton(isMaximized());
    QMainWindow::changeEvent(e);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e)
{
    // CanvasView handles drag/drop when a document tab exists.
    if (m_canvas) {
        e->ignore();
        return;
    }
    if (!extractValidDropImagePaths(e->mimeData()).isEmpty()) {
        e->acceptProposedAction();
        return;
    }
    e->ignore();
}

void MainWindow::dragMoveEvent(QDragMoveEvent* e)
{
    if (m_canvas) {
        e->ignore();
        return;
    }
    if (!extractValidDropImagePaths(e->mimeData()).isEmpty()) {
        e->acceptProposedAction();
        return;
    }
    e->ignore();
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent* e)
{
    Q_UNUSED(e)
}

void MainWindow::dropEvent(QDropEvent* e)
{
    // If there is an active canvas, let CanvasView process the drop.
    if (m_canvas) {
        e->ignore();
        return;
    }

    const QStringList paths = extractValidDropImagePaths(e->mimeData());
    if (paths.isEmpty()) {
        e->ignore();
        return;
    }

    // No document open at all: nothing to offer a choice for, open directly.
    if (!openDroppedImagesAsNewDocument(paths)) {
        e->ignore();
        return;
    }
    e->acceptProposedAction();
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        if (m_canvas && m_canvas->isTextEditing()) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            const int key = keyEvent->key();
            const Qt::KeyboardModifiers mods = keyEvent->modifiers();
            const bool ctrl = mods & Qt::ControlModifier;
            const bool alt  = mods & Qt::AltModifier;
            const bool meta = mods & Qt::MetaModifier;

            // Navigation keys: allow Shift (selection) and Ctrl (word) modifiers.
            const bool isNavKey =
                key == Qt::Key_Left || key == Qt::Key_Right ||
                key == Qt::Key_Up   || key == Qt::Key_Down  ||
                key == Qt::Key_Home || key == Qt::Key_End;

            // Plain editing keys (Backspace/Delete/Enter/Escape).
            const bool isEditKey =
                key == Qt::Key_Delete || key == Qt::Key_Backspace ||
                key == Qt::Key_Return || key == Qt::Key_Enter ||
                key == Qt::Key_Escape;

            // Printable typing — never a Ctrl/Alt/Meta combo, so that
            // single-key tool shortcuts (B, E, T, …) insert characters instead.
            const QString text = keyEvent->text();
            const bool isPrintable = !ctrl && !alt && !meta
                && !text.isEmpty() && text[0].isPrint();

            // Clipboard + select-all the text editor implements itself. Without
            // consuming these, the global Edit actions fire while editing — e.g.
            // Ctrl+V triggers "paste layer" and duplicates the layer instead of
            // pasting text. Ctrl+Z / Ctrl+S / … still pass through to the app.
            const bool isCtrlEditingCombo = ctrl && !alt && !meta &&
                (key == Qt::Key_A || key == Qt::Key_C ||
                 key == Qt::Key_X || key == Qt::Key_V);

            const bool consume =
                (isNavKey  && !alt && !meta) ||
                (isEditKey && !alt && !meta) ||
                isPrintable ||
                isCtrlEditingCombo;

            if (consume) {
                event->accept();
                return true;
            }
        }
    }
    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        auto* w = qobject_cast<QWidget*>(obj);
        const bool textInputFocused = w
            && (qobject_cast<QLineEdit*>(w)
                || qobject_cast<QTextEdit*>(w)
                || qobject_cast<QPlainTextEdit*>(w)
                || qobject_cast<QAbstractSpinBox*>(w));
        ShortcutManager::DispatchContext context;
        context.textInputFocused = textInputFocused;
        context.popupActive = QApplication::activePopupWidget() != nullptr;
        context.textCanvasEditingActive = m_canvas && m_canvas->isTextEditing();
        context.modalActive = m_canvas
            && (m_canvas->isDistortActive()
                || m_canvas->isFreeTransformActive()
                || (m_canvas->currentTool() == CanvasView::Tool::Crop && m_canvas->isCropActive()));
        context.canvasFocused = m_canvas && w && (w == m_canvas || m_canvas->isAncestorOf(w));
        context.layerPanelFocused = m_layerPanel && w
            && (w == m_layerPanel || m_layerPanel->isAncestorOf(w));
        context.toolActive = m_canvas != nullptr;
        context.moveToolActive = m_canvas && m_canvas->currentTool() == CanvasView::Tool::Move;
        context.maskEditActive = m_canvas && m_canvas->isEditingMask();
        if (auto* layer = m_doc ? m_doc->activeLayer() : nullptr)
            context.maskTargetActive = !layer->maskImage.isNull();

        if (ShortcutManager::instance()->dispatchShortcut(
                ShortcutManager::eventSequence(keyEvent), context)) {
            event->accept();
            return true;
        }

        if (keyEvent->key() == Qt::Key_Delete || keyEvent->key() == Qt::Key_Backspace) {
            if (!w) return false;
            // Let CanvasView handle Delete itself (text editing, etc.)
            if (w == m_canvas) return false;
            // Let text input widgets handle Delete normally
            if (textInputFocused)
                return false;
            // Otherwise intercept: CanvasView won't see this key event
            if (m_doc && m_ctrl) {
                if (m_doc->selection.active() && !m_doc->selection.isEmpty()) {
                    m_ctrl->executeTool("delete_selected", {});
                    return true;
                } else if (m_ctrl->activeLayer()) {
                    m_ctrl->executeTool("remove_layer", {});
                    return true;
                }
            }
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::onMinimize()
{
    showMinimized();
}

void MainWindow::onMaximizeRestore()
{
    if (isMaximized())
        showNormal();
    else
        showMaximized();
    if (m_titleBar)
        m_titleBar->updateMaximizeButton(isMaximized());
}

void MainWindow::onCloseWindow()
{
    close();
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    for (int i = static_cast<int>(m_tabs.size()) - 1; i >= 0; --i) {
        if (!ensureTabCanBeDiscardedOrSaved(i)) {
            e->ignore();
            return;
        }
    }
    // Put the real editing layout back before persisting it: the Home-only
    // all-hidden state must never be what the next session restores.
    setHomeDockMode(false);
    // Final, undebounced write, and the one that catches changes no signal
    // reports in time — a tab reordered inside its group moments before
    // quitting, or a window moved without being resized.
    if (m_workspace)
        m_workspace->saveNow();
    e->accept();
}

void MainWindow::setupAgents()
{
    m_presetManager->loadAll();
    if (m_generativeFillPanel)
        m_generativeFillPanel->reloadPresets();

    // The chat panel only lists Assistant presets; Generative presets are used
    // by the Generative Fill feature, not the chat agent.
    m_agentPanel->setAgentList(m_presetManager->presetNames(Assistant));
    m_agentPanel->setActiveAgent(m_presetManager->activeAssistantName());

    auto active = m_presetManager->activeAssistant();
    m_agentController->applyConfig(active);

    QString info = QString("%1 (%2)")
        .arg(active.provider.model)
        .arg(active.provider.displayName());
    m_agentPanel->setModelInfo(info);

    connect(m_presetManager, &AgentPresetManager::presetsChanged, this, [this]() {
        m_agentPanel->setAgentList(m_presetManager->presetNames(Assistant));
        m_agentPanel->setActiveAgent(m_presetManager->activeAssistantName());
        if (m_generativeFillPanel)
            m_generativeFillPanel->reloadPresets();
    });
}

void MainWindow::onAgentSelected(const QString& name)
{
    if (!m_presetManager->exists(name)) {
        return;
    }

    m_presetManager->setActivePreset(name);
    auto config = m_presetManager->activePreset();
    m_agentController->applyConfig(config);

    QString info = QString("%1 (%2)")
        .arg(config.provider.model)
        .arg(config.provider.displayName());
    m_agentPanel->setModelInfo(info);
    m_agentPanel->setActiveAgent(name);
}

void MainWindow::onGenerativeFill()
{
    if (!m_ctrl) return;
    if (m_presetManager->presetNames(Generative).isEmpty()) {
        AppMessageDialog::information(this, tr("Generative Fill"),
            tr("No Generative preset is configured. Open the AI Agent settings "
               "and create a preset of type \"Generative\" first."));
        return;
    }
    if (!m_generativeFillDock || !m_generativeFillPanel)
        return;

    m_generativeFillPanel->setController(m_ctrl);
    m_generativeFillPanel->reloadPresets();
    m_generativeFillDock->setFloating(true);
    if (m_toolBar) {
        const QPoint pos = m_toolBar->mapToGlobal(QPoint(0, m_toolBar->height()));
        m_generativeFillDock->move(pos);
    }
    m_generativeFillDock->show();
    m_generativeFillDock->raise();
    m_generativeFillDock->activateWindow();
}

void MainWindow::onConfigureAgent()
{
    showAppSettings(AppSettingsDialog::PageAIAgent, [this](int result) {
        if (result != QDialog::Accepted)
            return;
        m_presetManager->loadAll();
        m_agentPanel->setAgentList(m_presetManager->presetNames(Assistant));
        if (m_generativeFillPanel)
            m_generativeFillPanel->reloadPresets();

        QStringList assistants = m_presetManager->presetNames(Assistant);
        QString name = m_presetManager->activeAssistantName();
        if (m_presetManager->exists(name) &&
            m_presetManager->preset(name).kind == Assistant) {
            onAgentSelected(name);
        } else if (!assistants.isEmpty()) {
            onAgentSelected(assistants.first());
        }
    });
}

void MainWindow::applyMcpSettings()
{
    if (!m_mcpServer)
        return;

    const bool wantEnabled = McpSettings::enabled();
    const quint16 wantPort = McpSettings::port();

    if (!wantEnabled) {
        if (m_mcpServer->isListening())
            m_mcpServer->stop();
        return;
    }

    // Enabled: (re)bind only if not already listening on the desired port.
    if (m_mcpServer->isListening()) {
        if (m_mcpServer->serverPort() == wantPort)
            return;
        m_mcpServer->stop();
    }

    if (!m_mcpServer->start(wantPort))
        qWarning() << "Failed to start MCP server on port" << wantPort;
}

void MainWindow::onSettings()
{
    showAppSettings(AppSettingsDialog::PageGeneral, [this](int result) {
        if (result != QDialog::Accepted)
            return;
        reloadRulerGuideSettingsForTabs();
        updateRulerGuideActionStates();
    });
}

void MainWindow::onOpenAiSettings()
{
    showAppSettings(AppSettingsDialog::PageAIMachineLearning, [this](int) {
        // Provider/model settings may have changed: reset AI caches/sessions and
        // refresh the tool's availability + model list. Runs on cancel too, as it
        // did when this was an exec() whose result was ignored.
        if (m_canvas) {
            if (auto* ai = m_canvas->aiSelectionController())
                ai->onProviderOrSettingsChanged();
            m_toolBar->bindAiController(m_canvas->aiSelectionController());
            ensureAiControllerConnected();
        }
    });
}

void MainWindow::onCanvasContextMenuRequested(QPoint globalPos)
{
    if (!m_ctrl || !m_doc || !m_canvas) return;

    QMenu menu(this);

    int idx = m_ctrl->activeLayerIndex();
    bool hasLayer = idx >= 0;
    int flatCount = m_doc->flatCount();
    bool hasSelection = m_doc->selection.active() && !m_doc->selection.isEmpty();

    auto refreshAll = [this]() {
        if (m_canvas) { m_canvas->syncLayersToGpu(); m_canvas->update(); }
        refreshLayerPanel();
        refreshPropertiesPanel();
        updateLayerMenuState();
    };

    if (hasLayer) {
        // ── Layer ────────────────────────────────────────────────────
        menu.addAction(tr("Rename Layer"), [this, idx]() {
            if (!m_ctrl) return;
            auto* node = m_doc->nodeAt(idx);
            if (!node) return;
            bool ok;
            QString newName = QInputDialog::getText(
                this, tr("Rename"), tr("Name:"),
                QLineEdit::Normal, node->name, &ok);
            if (ok)
                m_ctrl->renameNode(idx, newName);
        });

        menu.addAction(m_duplicateLayerAction);
        menu.addAction(m_removeLayerAction);

        auto* convertAction = menu.addAction(tr("Convert Layer"));
        convertAction->setEnabled(m_rasterizeLayerAction->isEnabled());
        connect(convertAction, &QAction::triggered, this, &MainWindow::onMenuRasterizeLayer);

        // ── Smart Objects ────────────────────────────────────────────
        // Flat entries, not the submenu, and only the ones that apply to this
        // layer — a context menu answers "what can I do with THIS?", so a
        // common layer offers only Convert, and Relink never appears on
        // embedded content. The actions themselves are the very same QActions
        // the Layer menu uses, so there is one implementation and one place
        // (updateLayerMenuState) deciding what is enabled.
        if (m_smartObjectMenu && m_ctrl) {
            updateLayerMenuState();
            const auto smartData = m_ctrl->smartObjectDataAt(idx);
            const bool isLinked = smartData
                && smartData->mode == SmartObjectStorageMode::Linked;

            menu.addSeparator();
            if (!smartData) {
                menu.addAction(m_smartObjectConvertAction);
            } else {
                menu.addAction(m_smartObjectEditAction);
                menu.addAction(m_smartObjectViaCopyAction);
                menu.addAction(m_smartObjectReplaceAction);
                if (isLinked) {
                    menu.addAction(m_smartObjectRelinkAction);
                    menu.addAction(m_smartObjectUpdateAction);
                    menu.addAction(m_smartObjectToEmbeddedAction);
                } else {
                    menu.addAction(m_smartObjectToLinkedAction);
                }
                // No Rasterize entry here: "Convert Layer" above is already
                // this menu's rasterize action, and offering both would be two
                // names for one command.
            }
        }

        menu.addSeparator();

        // ── Organization ──────────────────────────────────────────────
        auto* bringForwardAction = menu.addAction(tr("Bring Forward"));
        bringForwardAction->setEnabled(idx > 0);
        connect(bringForwardAction, &QAction::triggered, this, [this, idx, refreshAll]() {
            if (!m_ctrl || !m_doc) return;
            m_ctrl->reorderNode(idx, idx - 1);
            refreshAll();
        });

        auto* sendBackwardAction = menu.addAction(tr("Send Backward"));
        sendBackwardAction->setEnabled(idx < flatCount - 1);
        connect(sendBackwardAction, &QAction::triggered, this, [this, idx, refreshAll]() {
            if (!m_ctrl || !m_doc) return;
            m_ctrl->reorderNode(idx, idx + 2);
            refreshAll();
        });

        auto* bringToFrontAction = menu.addAction(tr("Bring To Front"));
        bringToFrontAction->setEnabled(idx > 0);
        connect(bringToFrontAction, &QAction::triggered, this, [this, idx, refreshAll]() {
            if (!m_ctrl || !m_doc) return;
            m_ctrl->reorderNode(idx, 0);
            refreshAll();
        });

        auto* sendToBackAction = menu.addAction(tr("Send To Back"));
        sendToBackAction->setEnabled(idx < flatCount - 1);
        connect(sendToBackAction, &QAction::triggered, this, [this, idx, flatCount, refreshAll]() {
            if (!m_ctrl || !m_doc) return;
            m_ctrl->reorderNode(idx, flatCount);
            refreshAll();
        });

        menu.addSeparator();

        // ── Merge ────────────────────────────────────────────────────
        menu.addAction(m_mergeDownAction);
        menu.addAction(m_mergeVisibleAction);
        menu.addAction(m_flattenImageAction);

        menu.addSeparator();

        // ── Transform ────────────────────────────────────────────────
        auto* freeTransformAction = menu.addAction(tr("Free Transform"));
        freeTransformAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
        connect(freeTransformAction, &QAction::triggered, this, [this]() {
            if (auto* canvas = activeCanvas()) {
                if (canvas->isFreeTransformActive())
                    canvas->commitFreeTransform();
                else
                    canvas->beginFreeTransform();
            }
        });

        menu.addAction(m_flipHorizontalAction);
        menu.addAction(m_flipVerticalAction);

        menu.addSeparator();

        // ── Selection ────────────────────────────────────────────────
        auto* selectAllAction = menu.addAction(tr("Select All"));
        selectAllAction->setShortcut(QKeySequence::SelectAll);
        connect(selectAllAction, &QAction::triggered, this, [this]() {
            if (m_ctrl) m_ctrl->executeTool("select_all", {});
        });

        auto* deselectAction = menu.addAction(tr("Deselect"));
        deselectAction->setEnabled(hasSelection);
        connect(deselectAction, &QAction::triggered, this, [this]() {
            if (m_ctrl) m_ctrl->executeTool("deselect", {});
        });

        auto* invertSelAction = menu.addAction(tr("Invert Selection"));
        invertSelAction->setEnabled(hasSelection);
        connect(invertSelAction, &QAction::triggered, this, [this]() {
            if (m_ctrl) m_ctrl->executeTool("select_invert", {});
        });

        menu.addSeparator();

        // ── Clipboard ────────────────────────────────────────────────
        auto* cutAction = menu.addAction(tr("Cut"));
        cutAction->setShortcut(QKeySequence::Cut);
        cutAction->setEnabled(hasSelection);
        connect(cutAction, &QAction::triggered, this, [this]() {
            if (!m_ctrl || !m_doc) return;
            m_ctrl->copy();
            m_ctrl->executeTool("delete_selected", {});
            if (m_canvas) { m_canvas->syncLayersToGpu(); m_canvas->update(); }
        });

        auto* copyAction = menu.addAction(tr("Copy"));
        copyAction->setShortcut(QKeySequence::Copy);
        connect(copyAction, &QAction::triggered, this, [this]() {
            if (auto* ctrl = activeController()) ctrl->copy();
        });

        auto* pasteAction = menu.addAction(tr("Paste"));
        pasteAction->setShortcut(QKeySequence::Paste);
        connect(pasteAction, &QAction::triggered, this, [this]() {
            // paste() resolves system vs internal clipboard itself.
            if (auto* ctrl = activeController())
                ctrl->paste();
        });

    } else {
        // ── No layer selected ─────────────────────────────────────────
        auto* pasteAction = menu.addAction(tr("Paste"));
        pasteAction->setShortcut(QKeySequence::Paste);
        connect(pasteAction, &QAction::triggered, this, [this]() {
            if (auto* ctrl = activeController())
                ctrl->paste();
        });

        auto* selectAllAction = menu.addAction(tr("Select All"));
        selectAllAction->setShortcut(QKeySequence::SelectAll);
        connect(selectAllAction, &QAction::triggered, this, [this]() {
            if (m_ctrl) m_ctrl->executeTool("select_all", {});
        });

        menu.addSeparator();

        menu.addAction(m_flattenImageAction);
    }

    menu.exec(globalPos);
}

// ─────────────────────────────────────────────────────────────────────────
// Smart Objects
// ─────────────────────────────────────────────────────────────────────────

ImageController* MainWindow::rootControllerForContainer(const DocumentContainer* container) const
{
    if (!container)
        return nullptr;
    const int rootTab = tabIndexForDocument(container, container->rootId());
    if (rootTab < 0)
        return nullptr;
    return m_tabs[rootTab].controller.get();
}

void MainWindow::scheduleSmartObjectRefresh()
{
    if (!m_smartObjectRefreshTimer) {
        m_smartObjectRefreshTimer = new QTimer(this);
        m_smartObjectRefreshTimer->setSingleShot(true);
        connect(m_smartObjectRefreshTimer, &QTimer::timeout,
                this, &MainWindow::flushSmartObjectRefresh);
    }
    // Restarted on every change, so a stroke or a slider drag produces exactly
    // one refresh once the user pauses.
    m_smartObjectRefreshTimer->start(200);
}

void MainWindow::flushSmartObjectRefresh()
{
    // Every open project is rechecked, not just the one that reported a change.
    // Two cases need it and neither names a document:
    //   • a Linked Smart Object in A.hazor watching B.hazor, when the user edits
    //     (or saves) B.hazor in its own tab — A's container never hears about it;
    //   • undo/redo of Replace Contents, which swaps the document stored under
    //     a docId without any particular document "changing".
    // The pass is O(nodes) with no file I/O and only dispatches genuinely stale
    // instances, so running it wholesale once the user pauses is cheaper than
    // the bookkeeping needed to narrow it.
    for (auto& tab : m_tabs) {
        if (!tab.container || !tab.controller)
            continue;
        // One controller drives a container's cascade: the root document's.
        // Letting each tab drive its own would mean several render services
        // compositing the same documents, each unaware of the others' commits.
        if (tab.docId != tab.container->rootId())
            continue;
        tab.controller->refreshAllSmartObjectInstances();
    }
}

void MainWindow::onSmartObjectRenderCommitted(
    const std::shared_ptr<DocumentContainer>& container,
    const QStringList& affectedDocuments, bool marksDirty)
{
    if (!container)
        return;
    for (auto& tab : m_tabs) {
        if (tab.container != container || !affectedDocuments.contains(tab.docId))
            continue;
        if (tab.canvas) {
            tab.canvas->syncLayersToGpu();
            tab.canvas->update();
        }
    }
    // The render that just landed IS part of the file (it is what an
    // unresolvable link falls back to), so a document whose instances changed
    // no longer matches its .hazor. `marksDirty` is false only for the pass that
    // regenerates renders already stored on disk, right after a load —
    // flagging a project as edited just for opening it would be wrong.
    if (marksDirty && !affectedDocuments.isEmpty())
        markContainerRootDirty(container);
    // The instance's thumbnail and the Properties page both read the payload,
    // which just changed.
    refreshLayerPanel();
    refreshPropertiesPanel();
}

void MainWindow::editSmartObjectContents(int flatIndex)
{
    if (!m_ctrl || !m_doc)
        return;
    const auto data = m_ctrl->smartObjectDataAt(flatIndex);
    if (!data)
        return;
    m_ctrl->setActiveNode(flatIndex);

    if (data->mode == SmartObjectStorageMode::Linked) {
        // A Linked Smart Object's content is just another project: open it as a
        // normal tab (loadProjectAsNewTab reactivates the existing one when the
        // file is already open, which is the same container either way).
        const QString path = m_ctrl->smartObjectLinkedPathAt(flatIndex);
        if (path.isEmpty() || !QFileInfo(path).isFile()) {
            AppMessageDialog::warning(this, tr("Smart Object"),
                tr("The linked file \"%1\" could not be found. Use Relink… to point "
                   "this Smart Object at its file.")
                    .arg(QFileInfo(data->linkedPath).fileName()));
            return;
        }
        loadProjectAsNewTab(path);
        return;
    }

    auto container = m_ctrl->documentContainerPtr();
    if (!container)
        return;
    auto content = container->document(data->docId);
    if (!content) {
        AppMessageDialog::warning(this, tr("Smart Object"),
            tr("This Smart Object's document is missing from the project."));
        return;
    }

    // One tab per document: a second one would give the same Document two
    // command histories, and an undo in either would surprise the other.
    const int existing = tabIndexForDocument(container.get(), data->docId);
    if (existing >= 0) {
        m_tabWidget->setCurrentIndex(tabWidgetIndexForDocument(existing));
        return;
    }

    // The tab works on a COPY of the document, the way editing a separate
    // content document does. Until Ctrl+S commits it, the instances keep
    // showing the stored content and the project is genuinely unmodified —
    // which is also what makes "close the content tab and lose the
    // experiment" a real option.
    container->beginEditSession(data->docId, smartobject::cloneDocument(*content));

    auto& tab = createTabForDocument(content->name, container, data->docId);
    // The content tab has no file of its own — Ctrl+S commits into the
    // container, and only saving the project writes the .hazor (saveProjectForTab).
    tab.currentProjectPath.clear();
    tab.sourcePath.clear();
    tab.isDirty = false;
    m_activeTabIndex = documentIndexForTabWidget(m_tabWidget->currentIndex());
    m_doc = tab.document.get();
    m_ctrl = tab.controller.get();
    m_canvas = tab.canvas;
    if (m_canvas) {
        m_canvas->syncLayersToGpu();
        m_canvas->update();
    }
    refreshLayerPanel();
    refreshPropertiesPanel();
    refreshHistoryPanel();
    updateLayerMenuState();
    updateStatusFromDocument();
    updateDirtyUi();
}

void MainWindow::convertSelectionToSmartObject()
{
    if (!m_ctrl || !m_doc)
        return;
    std::vector<int> indices = m_doc->selectedList();
    if (indices.empty() && m_doc->activeFlatIndex >= 0)
        indices.push_back(m_doc->activeFlatIndex);
    if (m_ctrl->convertToSmartObject(indices) < 0)
        return;
    markActiveTabDirty(true);
    refreshLayersUi();
}

void MainWindow::placeSmartObject(bool linked)
{
    if (!m_ctrl || !m_doc)
        return;
    const QString path = QFileDialog::getOpenFileName(
        this,
        linked ? tr("Place Linked") : tr("Place Embedded"),
        QString(), tr("Hazor Studio (*.hazor *.hzs)"));
    if (path.isEmpty())
        return;

    const int index = linked ? m_ctrl->placeLinkedDocument(path)
                             : m_ctrl->placeEmbeddedDocument(path);
    if (index < 0)
        return;
    markActiveTabDirty(true);
    refreshLayersUi();
}

void MainWindow::runSmartObjectCommand(int flatIndex, LayerSmartObjectCommand command)
{
    if (!m_ctrl || !m_doc || flatIndex < 0)
        return;
    // Every slot below acts on the ACTIVE layer, so the layer the menu was
    // opened on becomes active first. That also matches what the user sees: the
    // row they right-clicked ends up selected.
    if (m_doc->activeFlatIndex != flatIndex) {
        m_ctrl->setActiveNode(flatIndex);
        refreshLayerPanel();
    }

    switch (command) {
    case LayerSmartObjectCommand::Convert:
        convertSelectionToSmartObject();
        break;
    case LayerSmartObjectCommand::ViaCopy:
        newSmartObjectViaCopy();
        break;
    case LayerSmartObjectCommand::EditContents:
        editSmartObjectContents(flatIndex);
        break;
    case LayerSmartObjectCommand::ReplaceContents:
        replaceActiveSmartObjectContents();
        break;
    case LayerSmartObjectCommand::RelinkToFile:
        relinkActiveSmartObject();
        break;
    case LayerSmartObjectCommand::UpdateModified:
        updateActiveSmartObjectContent();
        break;
    case LayerSmartObjectCommand::ConvertToLinked:
        convertActiveSmartObjectStorage(/*toLinked=*/true);
        break;
    case LayerSmartObjectCommand::EmbedLinked:
        convertActiveSmartObjectStorage(/*toLinked=*/false);
        break;
    case LayerSmartObjectCommand::Rasterize:
        onMenuRasterizeLayer();
        break;
    }
}

void MainWindow::newSmartObjectViaCopy()
{
    if (!m_ctrl || !m_doc)
        return;
    if (m_ctrl->newSmartObjectViaCopy(m_doc->activeFlatIndex) < 0)
        return;
    markActiveTabDirty(true);
    refreshLayersUi();
}

void MainWindow::replaceActiveSmartObjectContents()
{
    if (!m_ctrl || !m_doc)
        return;
    const int flatIndex = m_doc->activeFlatIndex;
    if (!m_ctrl->smartObjectDataAt(flatIndex))
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Replace Contents"), QString(), tr("Hazor Studio (*.hazor *.hzs)"));
    if (path.isEmpty())
        return;
    if (!m_ctrl->replaceSmartObjectContents(flatIndex, path))
        return;
    markActiveTabDirty(true);
    refreshLayersUi();
}

void MainWindow::relinkActiveSmartObject()
{
    if (!m_ctrl || !m_doc)
        return;
    const int flatIndex = m_doc->activeFlatIndex;
    if (!m_ctrl->smartObjectDataAt(flatIndex))
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Relink Smart Object"), QString(), tr("Hazor Studio (*.hazor *.hzs)"));
    if (path.isEmpty())
        return;
    if (!m_ctrl->relinkSmartObject(flatIndex, path))
        return;
    markActiveTabDirty(true);
    refreshLayersUi();
}

void MainWindow::updateActiveSmartObjectContent()
{
    if (!m_ctrl || !m_doc)
        return;
    if (m_ctrl->updateSmartObjectContent(m_doc->activeFlatIndex))
        refreshLayersUi();
}

void MainWindow::convertActiveSmartObjectStorage(bool toLinked)
{
    if (!m_ctrl || !m_doc)
        return;
    const int flatIndex = m_doc->activeFlatIndex;
    const auto data = m_ctrl->smartObjectDataAt(flatIndex);
    if (!data)
        return;

    bool ok = false;
    if (toLinked) {
        // Converting to Linked has to produce the external file first, so it
        // needs a path from the user before anything is changed.
        QString suggested = m_doc->nodeAt(flatIndex) ? m_doc->nodeAt(flatIndex)->name
                                                     : tr("Smart Object");
        if (!suggested.endsWith(QStringLiteral(".hazor"), Qt::CaseInsensitive))
            suggested += QStringLiteral(".hazor");
        QString path = QFileDialog::getSaveFileName(
            this, tr("Convert to Linked"), suggested, tr("Hazor Studio (*.hazor)"));
        if (path.isEmpty())
            return;
        if (!path.endsWith(QStringLiteral(".hazor"), Qt::CaseInsensitive))
            path += QStringLiteral(".hazor");
        ok = m_ctrl->convertSmartObjectToLinked(flatIndex, path);
    } else {
        ok = m_ctrl->convertSmartObjectToEmbedded(flatIndex);
    }
    if (!ok)
        return;
    markActiveTabDirty(true);
    refreshLayersUi();
}
