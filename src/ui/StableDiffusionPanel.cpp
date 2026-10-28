#include "StableDiffusionPanel.hpp"

#include "ai/sd/StableDiffusionTxt2Img.hpp"
#include "controller/ImageController.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QDebug>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace {
// Layer name for the inserted result, per the integration spec.
const char* kLayerName = "AI Generated";
}

StableDiffusionPanel::StableDiffusionPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("stableDiffusionPanel"));
    setAttribute(Qt::WA_StyledBackground, true);

    // One inference at a time, and the destructor waits on this pool.
    m_pool.setMaxThreadCount(1);

    buildUi();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &StableDiffusionPanel::applyTheme);
    applyTheme();
    setState(State::Ready);
}

StableDiffusionPanel::~StableDiffusionPanel()
{
    // Shut the inference down before any Qt object it could post to goes away.
    // cancelInFlight() only shortens the wait — waitForDone() is what makes the
    // worker's `this` capture safe, and Qt drops the queued result event with
    // the destroyed receiver.
    sdlocal::cancelInFlight();
    m_pool.waitForDone();
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
    m_ctrl = ctrl;
    // A generation already in flight keeps running: onGenerationFinished()
    // compares against m_pendingCtrl and discards a result whose document is
    // no longer the active one.
    if (!m_running)
        setState(State::Ready);
}

void StableDiffusionPanel::setState(State state, const QString& detail)
{
    QString text;
    switch (state) {
    case State::Ready:        text = tr("Ready");          break;
    case State::LoadingModel: text = tr("Loading model…");  break;
    case State::Generating:   text = tr("Generating…");     break;
    case State::Completed:    text = tr("Completed");      break;
    case State::Error:        text = tr("Error");          break;
    }
    if (!detail.isEmpty())
        text += QStringLiteral(" — ") + detail;
    m_status->setText(text);
}

void StableDiffusionPanel::onGenerateClicked()
{
    if (m_running)  // the button is disabled too; this is the belt to that braces
        return;

    if (!m_ctrl || !m_ctrl->document()) {
        setState(State::Error, tr("Open a document first."));
        return;
    }

    m_running = true;
    m_pendingCtrl = m_ctrl;
    m_generateBtn->setEnabled(false);
    setState(State::LoadingModel, sdlocal::modelPath());

    qInfo().noquote() << "[SD] generate requested | model=" << sdlocal::modelPath()
                      << "| prompt=" << sdlocal::prompt();

    (void) QtConcurrent::run(&m_pool, [this]() {
        auto onModelLoaded = [this]() {
            QMetaObject::invokeMethod(this, [this]() {
                if (m_running)
                    setState(State::Generating);
            }, Qt::QueuedConnection);
        };

        const sdlocal::Txt2ImgResult result = sdlocal::runHardcodedTxt2Img(onModelLoaded);

        QMetaObject::invokeMethod(this, [this, result]() {
            onGenerationFinished(result.image, result.error,
                                 result.loadMs, result.generateMs);
        }, Qt::QueuedConnection);
    });
}

void StableDiffusionPanel::onGenerationFinished(const QImage& image, const QString& error,
                                                qint64 loadMs, qint64 generateMs)
{
    m_running = false;
    m_generateBtn->setEnabled(true);

    if (!error.isEmpty()) {
        setState(State::Error, error);
        return;
    }

    // The document the run was started against has to still be the active one:
    // the pixels belong to it, and pushing them into a different (or destroyed)
    // document would be worse than losing them.
    if (!m_pendingCtrl || m_pendingCtrl != m_ctrl || !m_ctrl->document()) {
        setState(State::Error, tr("The document is no longer open; the image was not inserted."));
        qWarning() << "[SD] result dropped: document closed or changed during generation";
        return;
    }

    // importImage() is Hazor's existing "QImage → new pixel layer" path: it
    // centres the layer on the canvas, inserts it above the current stack,
    // pushes the undoable AddLayerCommand, selects it and bumps the
    // composition generation so canvas and Layers panel refresh.
    if (!m_ctrl->importImage(image, QString::fromUtf8(kLayerName))) {
        setState(State::Error, tr("The layer could not be created."));
        return;
    }

    setState(State::Completed, tr("%1×%2 — load %3 s, generate %4 s")
        .arg(image.width())
        .arg(image.height())
        .arg(loadMs / 1000.0, 0, 'f', 1)
        .arg(generateMs / 1000.0, 0, 'f', 1));
    qInfo().noquote() << "[SD] inserted layer |" << image.size()
                      << "| loadMs=" << loadMs << "| generateMs=" << generateMs;
}
