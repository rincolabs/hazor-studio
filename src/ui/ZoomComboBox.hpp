#pragma once

#include <QComboBox>

class QEvent;
class QKeyEvent;

class ZoomComboBox : public QComboBox {
    Q_OBJECT

public:
    explicit ZoomComboBox(QWidget* parent = nullptr);

    void setZoomRange(float minZoom, float maxZoom);
    void setZoom(float zoom);
    float zoom() const { return m_lastValidZoom; }

    static QString formatZoom(float zoom);
    static bool parseZoomText(const QString& text, float minZoom, float maxZoom, float& zoom);

signals:
    void zoomRequested(float zoom);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void beginEditing();
    void commitEditorText();
    void cancelEditing();
    void restoreLastValidZoom();
    void selectEditorText();
    static QString editorTextForZoom(float zoom);

    float m_minZoom = 0.01f;
    float m_maxZoom = 100.0f;
    float m_lastValidZoom = 1.0f;
    bool m_updating = false;
    bool m_editing = false;
    bool m_committing = false;
};
