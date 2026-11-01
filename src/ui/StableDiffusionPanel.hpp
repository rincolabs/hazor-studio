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

// Experimental "AI Tools" dock panel: links into the Neural Flow node editor
// and the Hazor AI Center, backed by the local Stable Diffusion module
// (src/ai/sd/) via SdCanvasBridge/SdWorkflowExecutor, which drop a generated
// result into the active document as a new pixel layer.
//
// Only built when ENABLE_STABLE_DIFFUSION=ON, so MainWindow creates it behind
// the same HAVE_STABLE_DIFFUSION guard.
//
// The Neural Flow button is present only when that panel was built
// (HAVE_NEURAL_FLOW).
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
    // Same reasoning: MainWindow owns the AI Center window.
    void aiCenterRequested();

private:
    void buildUi();
    void applyTheme();
    void setStatus(const QString& text, const QString& detail = QString());
    void onStateChanged(sdlocal::SdGenerationState state, const QString& detail);
    void onFinished(const sdlocal::SdWorkflowRunResult& result);

    sdlocal::SdCanvasBridge* m_bridge = nullptr;
    sdlocal::SdWorkflowExecutor* m_executor = nullptr;

    QLabel* m_title = nullptr;
    QLabel* m_status = nullptr;
    // Only built when the embedded interfaces are compiled in; null otherwise.
    QPushButton* m_neuralFlowBtn = nullptr;
    QPushButton* m_aiCenterBtn = nullptr;
};
