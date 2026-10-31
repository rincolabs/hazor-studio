#pragma once

#include <QWidget>

class ImageController;
class NeuralFlowBridge;
class QLabel;
class QStackedLayout;
class QWebChannel;
class WebViewHost;

// The "Hazor Neural Flow" workspace panel: the Vue node editor from
// embedded-ui/, hosted in the Qt WebEngine view Hazor already uses for the AI
// Agent panel.
//
// This class is only the host. It creates the view, registers the one
// QWebChannel object the page may talk to (NeuralFlowBridge) and loads the
// compiled application out of the Qt resource system — no second embedded
// browser, no HTTP server, no network access. Everything the editor does to a
// document goes through the bridge into the existing Stable Diffusion workflow
// controller.
//
// Built only when the Stable Diffusion module and the embedded interfaces are
// both compiled in, which is why MainWindow creates it behind HAVE_NEURAL_FLOW.
class NeuralFlowPanel : public QWidget {
    Q_OBJECT

public:
    explicit NeuralFlowPanel(QWidget* parent = nullptr);
    ~NeuralFlowPanel() override;

    // (Re)binds the panel to the active document's controller; nullptr means no
    // document is open. Same lifecycle as the other panels.
    void setController(ImageController* ctrl);

protected:
    // The page is loaded on the first show, not in the constructor. Two reasons:
    // a Chromium renderer should not be started for a panel nobody opened, and
    // the first thing the page does is ask the bridge for the registered models
    // — which would make the Stable Diffusion registry scan its directories
    // during Hazor's startup, the one thing it is built never to do.
    void showEvent(QShowEvent* event) override;

private:
    void applyTheme();
    void showLoadFailure(const QString& detail);

    WebViewHost* m_webHost = nullptr;
    NeuralFlowBridge* m_bridge = nullptr;
    QWebChannel* m_channel = nullptr;
    QStackedLayout* m_stack = nullptr;
    QLabel* m_failure = nullptr;
    bool m_loadStarted = false;
};
