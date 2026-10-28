#pragma once

#include <QPointer>
#include <QThreadPool>
#include <QWidget>

class ImageController;
class QLabel;
class QPushButton;

// Experimental "Stable Diffusion" dock panel: one Generate button that runs a
// hardcoded local txt2img through stable-diffusion.cpp (see
// src/ai/sd/StableDiffusionTxt2Img.hpp) and drops the result into the active
// document as a new pixel layer.
//
// Only built when ENABLE_STABLE_DIFFUSION=ON, so MainWindow creates it behind
// the same HAVE_STABLE_DIFFUSION guard. There is nothing to configure here on
// purpose — prompt, model and sampling parameters live in the C++ constants.
class StableDiffusionPanel : public QWidget {
    Q_OBJECT
public:
    explicit StableDiffusionPanel(QWidget* parent = nullptr);
    ~StableDiffusionPanel() override;

    // (Re)binds the panel to the active document's controller; nullptr means no
    // document is open. Same lifecycle as the other right-side panels.
    void setController(ImageController* ctrl);

private:
    enum class State { Ready, LoadingModel, Generating, Completed, Error };

    void buildUi();
    void applyTheme();
    void setState(State state, const QString& detail = QString());
    void onGenerateClicked();
    // Runs on the GUI thread once the worker posts its result back.
    void onGenerationFinished(const QImage& image, const QString& error,
                              qint64 loadMs, qint64 generateMs);

    ImageController* m_ctrl = nullptr;
    // The controller the in-flight generation was started against: the result is
    // dropped unless this is still the active one, so a document closed or a tab
    // switched mid-generation cannot have pixels pushed into it.
    QPointer<ImageController> m_pendingCtrl;

    QLabel* m_title = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_generateBtn = nullptr;

    // One worker thread, and the destructor waits on it, so the inference can
    // never outlive this widget and touch a destroyed Qt object.
    QThreadPool m_pool;
    bool m_running = false;
};
