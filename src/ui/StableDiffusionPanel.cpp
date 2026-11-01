#include "StableDiffusionPanel.hpp"

#include "ai/sd/SdCanvasBridge.hpp"
#include "ai/sd/SdWorkflowExecutor.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

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

#ifdef HAVE_NEURAL_FLOW
    // The way into the node editor. Not disabled while a generation runs: the
    // editor is where the user watches it, and the run itself is on a worker
    // thread.
    m_neuralFlowBtn = new QPushButton(tr("Neural Flow"), this);
    m_neuralFlowBtn->setCursor(Qt::PointingHandCursor);
    m_neuralFlowBtn->setToolTip(tr("Open the Neural Flow node editor"));
    connect(m_neuralFlowBtn, &QPushButton::clicked,
            this, &StableDiffusionPanel::neuralFlowRequested);
    lay->addWidget(m_neuralFlowBtn);
#endif

    m_aiCenterBtn = new QPushButton(tr("AI Center"), this);
    m_aiCenterBtn->setCursor(Qt::PointingHandCursor);
    m_aiCenterBtn->setToolTip(tr("Open the Hazor AI Center"));
    connect(m_aiCenterBtn, &QPushButton::clicked,
            this, &StableDiffusionPanel::aiCenterRequested);
    lay->addWidget(m_aiCenterBtn);

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
