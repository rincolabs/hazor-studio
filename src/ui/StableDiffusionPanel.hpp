#pragma once

#include "ai/sd/SdGenerationTypes.hpp"

#include <QWidget>

class ImageController;
class QLabel;
class QPushButton;

namespace sdlocal {
class SdCanvasBridge;
class SdWorkflowExecutor;
struct SdWorkflowRunResult;
}

// Experimental "Stable Diffusion" dock panel: one Generate button that runs a
// fixed txt2img workflow through the local Stable Diffusion module
// (src/ai/sd/) and drops the result into the active document as a new pixel
// layer.
//
// Only built when ENABLE_STABLE_DIFFUSION=ON, so MainWindow creates it behind
// the same HAVE_STABLE_DIFFUSION guard. There is still nothing to configure
// here on purpose — the prompt and the sampling parameters are C++ constants,
// and the model comes from SdModelManager. What changed underneath is that the
// button now goes through the workflow controller rather than calling the
// engine directly, which is what keeps that path exercised: parse, validate,
// resolve, generate, insert.
//
// The one thing here that is not a test harness is the Neural Flow button: it
// is how the node editor is opened, so it is present whenever that panel was
// built (HAVE_NEURAL_FLOW).
class StableDiffusionPanel : public QWidget {
    Q_OBJECT
public:
    explicit StableDiffusionPanel(QWidget* parent = nullptr);
    ~StableDiffusionPanel() override;

    // (Re)binds the panel to the active document's controller; nullptr means no
    // document is open. Same lifecycle as the other right-side panels.
    void setController(ImageController* ctrl);

signals:
    // The user asked for the node editor. MainWindow owns the dock, so the
    // panel only reports the intent — opening it from here would mean this
    // widget knowing about the docking system.
    void neuralFlowRequested();

private:
    void buildUi();
    void applyTheme();
    void setStatus(const QString& text, const QString& detail = QString());
    void onGenerateClicked();
    void onStateChanged(sdlocal::SdGenerationState state, const QString& detail);
    void onFinished(const sdlocal::SdWorkflowRunResult& result);

    sdlocal::SdCanvasBridge* m_bridge = nullptr;
    sdlocal::SdWorkflowExecutor* m_executor = nullptr;

    QLabel* m_title = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_generateBtn = nullptr;
    // Only built when the embedded interfaces are compiled in; null otherwise.
    QPushButton* m_neuralFlowBtn = nullptr;
};
