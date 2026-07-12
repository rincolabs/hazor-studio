#pragma once

#include <QWidget>
#include <QVector>
#include <optional>

#include "animation/AnimationKeyframe.hpp"
#include "animation/RasterCelModel.hpp"

class ImageController;
class TimelineView;
class AppCheckBox;
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;

class TimelinePanel final : public QWidget {
    Q_OBJECT
public:
    explicit TimelinePanel(QWidget* parent = nullptr);
    void setController(ImageController* controller);

public slots:
    void refresh();

private:
    void buildUi();
    void updatePlayButton();

    ImageController* m_controller = nullptr;
    QVector<QMetaObject::Connection> m_connections;
    QPushButton* m_play = nullptr;
    QSpinBox* m_frame = nullptr;
    QDoubleSpinBox* m_fps = nullptr;
    QSpinBox* m_start = nullptr;
    QSpinBox* m_end = nullptr;
    AppCheckBox* m_loop = nullptr;
    AppCheckBox* m_autoKey = nullptr;
    TimelineView* m_view = nullptr;
    bool m_copiedPropertyKey = false;
    anim::Keyframe m_propertyClipboard;
    bool m_copiedRasterKey = false;
    std::optional<anim::RasterCelContent> m_rasterClipboard;
};
