#include <QApplication>
#include <QProxyStyle>
#include <QStyle>
#include <QStyleFactory>
#include <QFileInfo>
#include <QIcon>
#include <QSurfaceFormat>
#include <QTimer>
#include <QDebug>
#include <QImageReader>
#include <QFileOpenEvent>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <memory>
#include "ui/MainWindow.hpp"
#include "ui/SplashScreen.hpp"
#include "mcp/McpServer.hpp"
#include "async/AsyncJobSystem.hpp"
#include "core/AppPaths.hpp"
#include "core/CrashHandler.hpp"
#include "core/Version.hpp"
#include "brush/import/BrushImportManager.hpp"
#include "raw/RawEngine.hpp"

class NoDialogButtonIconsStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;
    QIcon standardIcon(StandardPixmap sp, const QStyleOption* opt = nullptr,
                       const QWidget* widget = nullptr) const override {
        switch (sp) {
        case SP_DialogOkButton:
        case SP_DialogCancelButton:
        case SP_DialogApplyButton:
        case SP_DialogCloseButton:
        case SP_DialogResetButton:
        case SP_DialogSaveButton:
        case SP_DialogDiscardButton:
        case SP_DialogYesButton:
        case SP_DialogNoButton:
        case SP_MessageBoxInformation:
        case SP_MessageBoxWarning:
        case SP_MessageBoxCritical:
        case SP_MessageBoxQuestion:
            return QIcon();
        default:
            return QProxyStyle::standardIcon(sp, opt, widget);
        }
    }
};

// Embeds Inter (Regular/Medium/SemiBold/Bold) from Qt resources so the UI
// typography doesn't depend on the font being installed on the host OS.
// Theme::fontFamily names this same family; see src/theme/Theme.hpp.
static void registerInterFonts()
{
    static const char* kWeights[] = {
        ":/fonts/Inter-Regular.ttf",
        ":/fonts/Inter-Medium.ttf",
        ":/fonts/Inter-SemiBold.ttf",
        ":/fonts/Inter-Bold.ttf",
    };
    for (const char* path : kWeights) {
        if (QFontDatabase::addApplicationFont(QString::fromLatin1(path)) == -1)
            qWarning() << "Failed to load embedded font" << path;
    }
}

static bool isSupportedBrushImportFile(const QString& filePath)
{
    static BrushImportManager manager;
    static bool registered = false;
    if (!registered) {
        manager.registerDefaultAdapters();
        registered = true;
    }
    return QFileInfo(filePath).isFile() && !manager.adaptersForFile(filePath).isEmpty();
}

static bool isHazorProjectFile(const QString& filePath)
{
    // .hzs is the old extension, kept readable for backward compatibility;
    // .hazor is the only one anything now writes.
    return (filePath.endsWith(QStringLiteral(".hazor"), Qt::CaseInsensitive)
            || filePath.endsWith(QStringLiteral(".hzs"), Qt::CaseInsensitive))
        && QFileInfo(filePath).isFile();
}

// On macOS the OS delivers "Open With" file paths through QFileOpenEvent rather
// than argv. This filter buffers any paths that arrive before the MainWindow is
// ready, then forwards them once it is set.
class FileOpenEventFilter : public QObject {
public:
    using QObject::QObject;

    void setWindow(MainWindow* window)
    {
        m_window = window;
        flushPending();
    }

    QStringList takePending()
    {
        const QStringList pending = m_pending;
        m_pending.clear();
        return pending;
    }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override
    {
        if (event->type() == QEvent::FileOpen) {
            const QString path = static_cast<QFileOpenEvent*>(event)->file();
            if (isHazorProjectFile(path)) {
                if (m_window)
                    m_window->openProjectFiles({ path });
                else
                    m_pending << path;
                return true;
            }
        }
        return QObject::eventFilter(obj, event);
    }

private:
    void flushPending()
    {
        if (m_window && !m_pending.isEmpty()) {
            m_window->openProjectFiles(m_pending);
            m_pending.clear();
        }
    }

    MainWindow* m_window = nullptr;
    QStringList m_pending;
};

int main(int argc, char* argv[])
{
    CrashHandler::install();
    QApplication app(argc, argv);

    registerInterFonts();
    QApplication::setFont(QFont(QStringLiteral("Inter")));

    // Force the Fusion base style on every platform so the app looks identical
    // regardless of the host desktop (Windows native, KDE Breeze, GTK, …). Fusion
    // is compiled into QtWidgets, so it is always available. The proxy only
    // suppresses the standard dialog-button icons; all custom theming is still
    // driven by ThemeManager / QSS on top of this base.
    app.setStyle(new NoDialogButtonIconsStyle(QStringLiteral("Fusion")));

    // Anchor the palette to Fusion's own standard palette. The host platform
    // theme (GNOME/GTK, KDE, Windows) otherwise injects its system colors on top
    // of Fusion, so the app would look different per desktop. We keep the platform
    // theme itself active (it is what enables the system's NATIVE file dialogs),
    // and pin only the palette here. The QSS in ThemeBuilder references palette()
    // roles, so pinning the palette is what makes widgets render identically
    // everywhere while native dialogs still come from the host desktop.
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        app.setPalette(fusion->standardPalette());
        delete fusion;
    }

    // Standard Qt application identity: organization Rincolabs, application
    // Hazor Studio. All QSettings (default-constructed) and the per-app data
    // location (AppPaths) resolve under this identity:
    //   settings -> ~/.config/Rincolabs/Hazor Studio.conf
    //   data     -> ~/.local/share/Rincolabs/Hazor Studio/
    QApplication::setOrganizationName(QStringLiteral("Rincolabs"));
    QApplication::setApplicationName(QStringLiteral("Hazor Studio"));
    QApplication::setApplicationVersion(QStringLiteral(HAZOR_VERSION_STRING));

    // File paths handed to us by the OS (extension association / "Open With").
    // *.hazor projects open as new tabs; supported brush files open the import
    // dialog. On macOS these instead arrive via QFileOpenEvent, handled below.
    QStringList initialBrushImportFiles;
    QStringList initialProjectFiles;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (isHazorProjectFile(args[i]))
            initialProjectFiles << args[i];
        else if (isSupportedBrushImportFile(args[i]))
            initialBrushImportFiles << args[i];
    }

    auto* fileOpenFilter = new FileOpenEventFilter(&app);
    app.installEventFilter(fileOpenFilter);

    app.setWindowIcon(QIcon(":/icons/app-icon.png"));
    QImageReader::setAllocationLimit(0); // remove Qt's default 128 MB decoded-image cap
    AsyncJobSystem::create();

    // Process-wide RawEngine startup (Fase 5): safe to call even when
    // HAVE_RAWENGINE isn't compiled in (isAvailable() just stays false) or
    // when the resource directory doesn't exist yet. Must run once, before
    // any RAW file is opened (registerDefaultImageCodecs()/onOpenFile()).
    RawEngine::initialize(AppPaths::rawEngineResourceDir().toStdString(),
                          AppPaths::rawUserDir().toStdString(),
                          AppPaths::lensfunDatabaseDir().toStdString());

    QSurfaceFormat fmt;
    fmt.setSwapInterval(1);
    fmt.setSamples(0);
    QSurfaceFormat::setDefaultFormat(fmt);

    SplashScreen splash;
    splash.setVersion(QStringLiteral("v") + QApplication::applicationVersion());
    splash.setProgress(5, "Starting application...");

    std::unique_ptr<MainWindow> window;

    QObject::connect(&splash, &SplashScreen::firstPainted, &app, [&]() {
        splash.setProgress(15, "Initializing application...");

        splash.setProgress(35, "Preparing your workspace...");
        window = std::make_unique<MainWindow>();

        fileOpenFilter->setWindow(window.get());
        QObject::connect(&splash, &SplashScreen::finished, window.get(),
                         [&window, initialBrushImportFiles, initialProjectFiles, fileOpenFilter]() {
            window->showMaximized();

            // Merge any *.hazor paths that arrived via QFileOpenEvent (macOS) before
            // the window existed with those parsed from argv (Windows/Linux).
            QStringList projectFiles = initialProjectFiles;
            projectFiles += fileOpenFilter->takePending();

            QTimer::singleShot(0, window.get(), [&window, projectFiles]() {
                if (!projectFiles.isEmpty())
                    window->openProjectFiles(projectFiles);
                // MainWindow starts on its permanent Home tab. A directly
                // supplied project selects its document tab; no blank document
                // is manufactured when startup has no file association.
            });

            if (!initialBrushImportFiles.isEmpty()) {
                QTimer::singleShot(0, window.get(), [&window, initialBrushImportFiles]() {
                    window->openBrushImportDialog(initialBrushImportFiles);
                });
            }
        });

        splash.setProgress(65, "Starting MCP server...");
        McpServer* mcp = new McpServer(window->toolExecutor(), window.get());
        window->setMcpServer(mcp);
        window->applyMcpSettings();

        splash.setProgress(90, "Preparing canvas...");

        const auto finishSplash = [&splash]() {
            splash.setProgress(98, "Ready.");
            splash.finish();
        };

        if (window->isCustomShapeIconPreloadFinished()) {
            finishSplash();
        } else {
            QObject::connect(window.get(), &MainWindow::customShapeIconPreloadFinished,
                             &splash, finishSplash);
            splash.setProgress(95, "Loading custom shapes...");
        }
    });

    splash.show();
    splash.raise();

    int ret = app.exec();
    window.reset();
    // After window.reset(): RawDevelopService's cached RawSessions (and the
    // rtengine InitialImage/StagedImageProcessor behind them) are owned by
    // the documents' ImageControllers, and rtengine::cleanup() tears down the
    // caches those sessions read from.
    RawEngine::shutdown();
    AsyncJobSystem::destroy();
    return ret;
}
