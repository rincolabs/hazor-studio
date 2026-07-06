#include "AboutDialog.hpp"
#include "AppDiagnostics.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QClipboard>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

// ── Internal helpers ──────────────────────────────────────────────────────────

namespace {

void addSection(QGridLayout* grid, int& row, const QString& title)
{
    if (row > 0) {
        auto* gap = new QLabel(grid->parentWidget());
        gap->setFixedHeight(6);
        grid->addWidget(gap, row++, 0, 1, 2);
    }
    auto* lbl = new QLabel(title, grid->parentWidget());
    lbl->setProperty("aboutRole", "section");
    grid->addWidget(lbl, row++, 0, 1, 2);
}

void addRow(QGridLayout* grid, int& row, const QString& key, const QString& value)
{
    auto* keyLbl = new QLabel(key + QStringLiteral(":"), grid->parentWidget());
    keyLbl->setAlignment(Qt::AlignRight | Qt::AlignTop);
    keyLbl->setProperty("aboutRole", "key");

    auto* valLbl = new QLabel(value.isEmpty() ? QStringLiteral("—") : value,
                              grid->parentWidget());
    valLbl->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    valLbl->setWordWrap(true);
    valLbl->setTextInteractionFlags(Qt::TextBrowserInteraction);
    valLbl->setOpenExternalLinks(true);
    valLbl->setProperty("aboutRole", "value");

    grid->addWidget(keyLbl, row, 0);
    grid->addWidget(valLbl, row, 1);
    ++row;
}

} // namespace

// ── AboutDialog ───────────────────────────────────────────────────────────────

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    m_diag = AppDiagnostics::collect();
    setWindowTitle(tr("About %1").arg(m_diag.appName));
    setMinimumSize(540, 400);
    resize(600, 520);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    setupUi();
    applyStyleSheet();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &AboutDialog::applyStyleSheet);
}

void AboutDialog::setupUi()
{
    const AppDiagnostics& d = m_diag;

    auto* mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);

    // ── Scroll area ──────────────────────────────────────────────
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget(scroll);
    scroll->setWidget(content);

    auto* contentLay = new QVBoxLayout(content);
    contentLay->setContentsMargins(28, 20, 28, 20);
    contentLay->setSpacing(0);

    // ── App icon ──────────────────────────────────────────────────
    auto* iconLbl = new QLabel(content);
    const QPixmap pix(QStringLiteral(":/icons/app-icon.png"));
    if (!pix.isNull())
        iconLbl->setPixmap(pix.scaledToWidth(100, Qt::SmoothTransformation));
    iconLbl->setAlignment(Qt::AlignHCenter);
    contentLay->addWidget(iconLbl);
    contentLay->addSpacing(10);

    auto* appNameLbl = new QLabel(d.appName, content);
    appNameLbl->setAlignment(Qt::AlignHCenter);
    appNameLbl->setProperty("aboutRole", "appName");
    contentLay->addWidget(appNameLbl);
    contentLay->addSpacing(16);

    // ── Info grid ─────────────────────────────────────────────────
    auto* gridHost = new QWidget(content);
    auto* grid = new QGridLayout(gridHost);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(5);
    grid->setColumnStretch(0, 0);
    grid->setColumnStretch(1, 1);
    grid->setColumnMinimumWidth(0, 160);

    int row = 0;

    // ── APPLICATION ──
    addSection(grid, row, tr("APPLICATION"));
    addRow(grid, row, tr("App"),          d.appName);
    addRow(grid, row, tr("Organization"),
           QStringLiteral("%1 (<a href=\"https://rincolabs.org/\" style=\"color:#4a9eff;text-decoration:none;\">rincolabs.org</a>)")
               .arg(QCoreApplication::organizationName()));
    addRow(grid, row, tr("Version"),      d.appVersion);
    addRow(grid, row, tr("Build date"),   d.buildDate);

    // ── SYSTEM ──
    addSection(grid, row, tr("SYSTEM"));
    addRow(grid, row, tr("OS"),           d.osName);
    addRow(grid, row, tr("Architecture"), d.cpuArchitecture);

    // ── LIBRARIES ──
    addSection(grid, row, tr("LIBRARIES"));
    addRow(grid, row, tr("Qt"),           d.qtVersion);
    addRow(grid, row, tr("OpenCV"),       d.opencvVersion);
    addRow(grid, row, tr("ONNX Runtime"),
           d.onnxCompiledIn ? d.onnxVersion : tr("Not compiled in"));

    // ── AI RUNTIME ──
    if (d.onnxCompiledIn) {
        addSection(grid, row, tr("AI RUNTIME"));
        addRow(grid, row, tr("Available providers"),
               d.onnxAvailableProviders.isEmpty()
                   ? tr("None")
                   : d.onnxAvailableProviders.join(QStringLiteral(", ")));
        addRow(grid, row, tr("Selected provider"), d.onnxSelectedProvider);
    }

    // ── ONNX MODELS ──
    addSection(grid, row, tr("ONNX MODELS"));
    addRow(grid, row, tr("Status"),
           d.modelsTotal == 0
               ? tr("No models configured")
               : tr("%1 of %2 found").arg(d.modelsFound).arg(d.modelsTotal));

    // ── REAL-ESRGAN ──
    addSection(grid, row, tr("REAL-ESRGAN"));
    addRow(grid, row, tr("Binary path"), d.esrganBinaryPath);
    addRow(grid, row, tr("Binary"),
           d.esrganBinaryFound ? tr("Found") : tr("Not found"));
    addRow(grid, row, tr("Models"),
           d.esrganModelsFound ? tr("Found") : tr("Not found"));

    // ── PATHS ──
    addSection(grid, row, tr("PATHS"));
    // addRow(grid, row, tr("Models"),
    //        d.modelsPath.isEmpty() ? tr("Not configured") : d.modelsPath);
    addRow(grid, row, tr("Cache"),  d.cachePath);
    addRow(grid, row, tr("Config"), d.configPath);

    // ── GPU ──
    if (!d.gpuName.isEmpty()) {
        addSection(grid, row, tr("GPU"));
        addRow(grid, row, tr("Device"), d.gpuName);
    }

    grid->setRowStretch(row, 1);
    contentLay->addWidget(gridHost);
    contentLay->addStretch();
    mainLay->addWidget(scroll, 1);

    // ── Button bar ───────────────────────────────────────────────
    auto* btnBar = new QWidget(this);
    btnBar->setObjectName(QStringLiteral("aboutBtnBar"));
    auto* btnLay = new QHBoxLayout(btnBar);
    btnLay->setContentsMargins(12, 8, 12, 10);

    auto* copyBtn  = new QPushButton(tr("Copy"), btnBar);
    auto* closeBtn = new QPushButton(tr("Close"), btnBar);
    copyBtn->setFixedWidth(100);
    closeBtn->setFixedWidth(100);

    btnLay->addStretch();
    btnLay->addWidget(copyBtn);
    btnLay->addWidget(closeBtn);

    mainLay->addWidget(btnBar);

    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_diag.toText());
    });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void AboutDialog::applyStyleSheet()
{
    auto* th = ThemeManager::instance()->current();
    const QString bg       = th->colorSurface.name();
    const QString text     = th->colorTextPrimary.name();
    const QString textDim  = th->colorTextSecondary.name();
    const QString accent   = th->colorAccent.name();
    const QString border   = th->colorBorder.name();
    const QString btnBg    = th->colorBackgroundTertiary.name();
    const QString btnHover = th->colorSurfaceHover.name();

    setStyleSheet(QStringLiteral(
        "QDialog { background: %1; }"
        "QScrollArea, QScrollArea > QWidget > QWidget { background: %1; border: none; }"

        "QLabel[aboutRole='appName'] {"
        "  color: white;"
        "  font-size: 20px;"
        "  font-weight: bold;"
        "}"
        "QLabel[aboutRole='section'] {"
        "  color: %4;"
        "  font-weight: bold;"
        "  font-size: 10px;"
        "  letter-spacing: 1px;"
        "  padding-top: 4px;"
        "}"
        "QLabel[aboutRole='key'] {"
        "  color: %3;"
        "  font-size: 13px;"
        "}"
        "QLabel[aboutRole='value'] {"
        "  color: %2;"
        "  font-size: 13px;"
        "}"

        "QWidget#aboutBtnBar {"
        "  background: %1;"
        "  border-top: 1px solid %5;"
        "}"
        "QPushButton {"
        "  background: %6;"
        "  color: %2;"
        "  border: 1px solid %5;"
        "  border-radius: 4px;"
        "  padding: 4px 12px;"
        "  font-size: 13px;"
        "}"
        "QPushButton:hover { background: %7; }"
    ).arg(bg, text, textDim, accent, border, btnBg, btnHover));
}
