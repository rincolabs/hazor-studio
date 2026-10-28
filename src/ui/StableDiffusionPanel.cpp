#include "StableDiffusionPanel.hpp"

#include "ai/sd/SdCanvasBridge.hpp"
#include "ai/sd/SdWorkflow.hpp"
#include "ai/sd/SdWorkflowExecutor.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QDebug>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

// ── The fixed test configuration ─────────────────────────────────────────────
// Still constants, and still no picker: this panel exists to prove the module
// works end to end, not to be an interface. What is no longer here is the model
// path — SdModelManager owns that now, which is why no absolute path (valid on
// exactly one machine) is committed any more. Register a checkpoint, or point
// HAZOR_SD_MODEL at one, and the "default" model id below resolves to it.
const char* kPrompt =
    "A beautiful mountain landscape at sunset, cinematic lighting, highly detailed";
const char* kNegativePrompt = "";

// Measured once with v1-5-pruned-emaonly.safetensors (SD 1.5, fp32, 4.27 GiB)
// on a 6-core i5-8400: 694 s for these 20 steps on the CPU. Two things dominate
// and neither is a misconfiguration — a txt_cfg of 7.0 runs the UNet twice per
// step (40 passes, not 20), and fp32 weights double the memory traffic a CPU
// backend is already bound by. A GPU build, or a checkpoint node with
// "weightType": "f16", is the lever for both.
constexpr int kWidth = 512;
constexpr int kHeight = 512;
constexpr int kSteps = 20;
constexpr qint64 kSeed = 42;       // fixed, so two runs of the test are comparable
constexpr float kCfgScale = 7.0f;

const char* kLayerName = "AI Generated";

} // namespace

StableDiffusionPanel::StableDiffusionPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("stableDiffusionPanel"));
    setAttribute(Qt::WA_StyledBackground, true);

    m_bridge = new sdlocal::SdCanvasBridge(this);
    m_executor = new sdlocal::SdWorkflowExecutor(m_bridge, this);
    connect(m_executor, &sdlocal::SdWorkflowExecutor::stateChanged,
            this, &StableDiffusionPanel::onStateChanged);
    connect(m_executor, &sdlocal::SdWorkflowExecutor::finished,
            this, &StableDiffusionPanel::onFinished);

    buildUi();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &StableDiffusionPanel::applyTheme);
    applyTheme();
    setStatus(tr("Ready"));
}

StableDiffusionPanel::~StableDiffusionPanel()
{
    // Shut the inference down before any Qt object it could post to goes away,
    // and free the inference context while there is still a thread to free it
    // on. The executor cancels, waits for its worker and releases the context.
    if (m_executor)
        m_executor->shutdown();
}

void StableDiffusionPanel::buildUi()
{
    const auto* t = ThemeManager::instance()->current();

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(t->spaceMD, t->spaceMD, t->spaceMD, t->spaceMD);
    lay->setSpacing(t->spaceSM);

    m_title = new QLabel(tr("Stable Diffusion"), this);
    m_title->setObjectName(QStringLiteral("sdPanelTitle"));
    lay->addWidget(m_title);

    m_generateBtn = new QPushButton(tr("Generate"), this);
    m_generateBtn->setCursor(Qt::PointingHandCursor);
    connect(m_generateBtn, &QPushButton::clicked,
            this, &StableDiffusionPanel::onGenerateClicked);
    lay->addWidget(m_generateBtn);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("sdPanelStatus"));
    m_status->setWordWrap(true);
    lay->addWidget(m_status);

    lay->addStretch(1);
}

void StableDiffusionPanel::applyTheme()
{
    const auto* t = ThemeManager::instance()->current();
    if (!t)
        return;
    setStyleSheet(QStringLiteral(R"(
        StableDiffusionPanel {
            background: %1;
            color: %2;
            font-family: "%3";
            font-size: %4px;
        }
        QLabel#sdPanelTitle {
            color: %2;
            font-size: %5px;
            font-weight: 600;
        }
        QLabel#sdPanelStatus {
            color: %6;
        }
        QPushButton {
            background: %7;
            color: %2;
            border: 1px solid %8;
            border-radius: %9px;
            padding: 5px 10px;
        }
        QPushButton:hover {
            background: %10;
        }
        QPushButton:disabled {
            color: %11;
            background: %7;
        }
    )")
        .arg(t->colorSurface.name())
        .arg(t->colorTextPrimary.name())
        .arg(t->fontFamily)
        .arg(t->fontSizeMD)
        .arg(t->fontSizeLG)
        .arg(t->colorTextSecondary.name())
        .arg(t->colorSurface.name())
        .arg(t->colorBorder.name())
        .arg(t->radiusSM)
        .arg(t->colorSurfaceHover.name())
        .arg(t->colorTextDisabled.name()));
    update();
}

void StableDiffusionPanel::setController(ImageController* ctrl)
{
    m_bridge->setController(ctrl);
    // A generation already in flight keeps running: the bridge holds its own
    // latch on the document it started against and drops a result whose
    // document is no longer the active one.
    if (!m_executor->busy())
        setStatus(tr("Ready"));
}

void StableDiffusionPanel::setStatus(const QString& text, const QString& detail)
{
    m_status->setText(detail.isEmpty() ? text
                                       : text + QStringLiteral(" — ") + detail);
}

void StableDiffusionPanel::onGenerateClicked()
{
    if (m_executor->busy())  // the button is disabled too; this is the belt to that braces
        return;

    // "default" resolves through SdModelManager on whatever machine runs this,
    // so the document stays portable and the registry stays the single place a
    // model is configured.
    const QByteArray workflow = sdlocal::txt2imgWorkflowJson(
        QStringLiteral("default"),
        QString::fromUtf8(kPrompt),
        QString::fromUtf8(kNegativePrompt),
        kWidth, kHeight, kSteps, kSeed, kCfgScale,
        QString::fromUtf8(kLayerName));

    qInfo().noquote() << "[SD] generate requested |"
                      << sdlocal::describeBackendEnvironment()
                      << "| prompt=" << QString::fromUtf8(kPrompt);

    QVector<sdlocal::SdWorkflowError> errors;
    if (!m_executor->run(workflow, &errors)) {
        setStatus(tr("Error"), sdlocal::describeWorkflowErrors(errors));
        return;
    }
    m_generateBtn->setEnabled(false);
}

void StableDiffusionPanel::onStateChanged(sdlocal::SdGenerationState state, const QString& detail)
{
    switch (state) {
    case sdlocal::SdGenerationState::Loading:
        setStatus(tr("Loading model…"), detail);
        break;
    case sdlocal::SdGenerationState::Generating:
        setStatus(tr("Generating…"));
        break;
    // Completed, Failed and Cancelled carry a result, so their text is written
    // by onFinished() where the timings and the device are known.
    case sdlocal::SdGenerationState::Completed:
    case sdlocal::SdGenerationState::Failed:
    case sdlocal::SdGenerationState::Cancelled:
    case sdlocal::SdGenerationState::Idle:
        break;
    }
}

void StableDiffusionPanel::onFinished(const sdlocal::SdWorkflowRunResult& result)
{
    m_generateBtn->setEnabled(true);

    if (result.generation.state == sdlocal::SdGenerationState::Cancelled) {
        setStatus(tr("Cancelled"));
        return;
    }
    if (!result.ok()) {
        setStatus(tr("Error"), result.firstError());
        return;
    }

    const sdlocal::SdGenerationResult& gen = result.generation;
    // The device is named rather than implied: a CPU run on a GPU build has to
    // read as a CPU run.
    setStatus(tr("Completed"),
              tr("%1×%2 on %3 — load %4 s, generate %5 s")
                  .arg(gen.size.width())
                  .arg(gen.size.height())
                  .arg(gen.deviceName)
                  .arg(gen.loadMs / 1000.0, 0, 'f', 1)
                  .arg(gen.generateMs / 1000.0, 0, 'f', 1));
}
