#pragma once

#include <QWidget>
#include "brush/DynamicsConfig.hpp"   // TextureConfig

class QComboBox;
class QSlider;
class QDoubleSpinBox;
class QSpinBox;
class QTabWidget;
class AppCheckBox;
class BrushResourceBrowser;

// A black→white gradient bar with two triangular handles for the texture cutoff
// range. Values are normalised [0,1]; emits rangeChanged while dragging.
class CutoffRangeSlider : public QWidget {
    Q_OBJECT
public:
    explicit CutoffRangeSlider(QWidget* parent = nullptr);
    void setRange(float lo, float hi);   // no signal
    float low() const { return m_low; }
    float high() const { return m_high; }
    QSize sizeHint() const override;

signals:
    void rangeChanged(float lo, float hi);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    QRect barRect() const;
    float xToValue(int x) const;
    int valueToX(float v) const;

    float m_low = 0.0f;
    float m_high = 1.0f;
    int m_drag = -1;   // 0 = low handle, 1 = high handle, -1 = none
};

// Texture component for the Brush Editor: a Texture tab (global
// texture library: grid + preview + search) and an Options tab (how the texture
// modulates the dab). Edits a TextureConfig and emits changed() live. Textures
// come from the global BrushTextureLibrary; the preset stores only the textureId.
class BrushTexturePanel : public QWidget {
    Q_OBJECT
public:
    explicit BrushTexturePanel(QWidget* parent = nullptr);

    void setConfig(const TextureConfig& c);   // no signal
    TextureConfig config() const { return m_config; }

    void reloadTextures();

signals:
    void changed();

private:
    QWidget* buildTextureTab();
    QWidget* buildOptionsTab();
    void onTextureSelected(const QString& id);
    void emitChanged();

    TextureConfig m_config;
    bool m_loading = false;

    QTabWidget* m_tabs = nullptr;

    // Texture tab — shared grid/preview/search/import component (BrushTexture mode).
    BrushResourceBrowser* m_browser = nullptr;

    // Options tab.
    QComboBox* m_mode = nullptr;
    AppCheckBox* m_soft = nullptr;
    QSlider* m_scale = nullptr;        QDoubleSpinBox* m_scaleSpin = nullptr;
    QComboBox* m_scaleUnit = nullptr;
    QSlider* m_brightness = nullptr;   QDoubleSpinBox* m_brightnessSpin = nullptr;
    QSlider* m_contrast = nullptr;     QDoubleSpinBox* m_contrastSpin = nullptr;
    QSlider* m_neutral = nullptr;      QDoubleSpinBox* m_neutralSpin = nullptr;
    QComboBox* m_cutoffPolicy = nullptr;
    CutoffRangeSlider* m_cutoff = nullptr;
    QSpinBox* m_hOffset = nullptr;     AppCheckBox* m_hRandom = nullptr;
    QSpinBox* m_vOffset = nullptr;     AppCheckBox* m_vRandom = nullptr;
    AppCheckBox* m_invert = nullptr;
    AppCheckBox* m_autoInvertEraser = nullptr;
};
