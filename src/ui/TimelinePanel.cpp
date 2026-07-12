#include "TimelinePanel.hpp"

#include "TimelineView.hpp"
#include "AppCheckBox.hpp"
#include "animation/AnimationModel.hpp"
#include "animation/AnimationCommands.hpp"
#include "animation/AnimationTransform.hpp"
#include "animation/PlaybackController.hpp"
#include "controller/ImageController.hpp"
#include "core/Document.hpp"
#include "core/LayerTreeNode.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>

TimelinePanel::TimelinePanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("TimelinePanel"));
    buildUi();
}

void TimelinePanel::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(4);
    auto* bar = new QHBoxLayout();
    bar->setSpacing(4);

    auto button = [this, bar](const QString& text, const QString& tip) {
        auto* b = new QPushButton(text, this);
        b->setToolTip(tip);
        b->setFixedHeight(26);
        bar->addWidget(b);
        return b;
    };
    auto* previous = button(QStringLiteral("|<"), tr("Previous frame"));
    m_play = button(QStringLiteral("▶"), tr("Play/Pause"));
    auto* stop = button(QStringLiteral("■"), tr("Stop"));
    auto* next = button(QStringLiteral(">|"), tr("Next frame"));

    bar->addWidget(new QLabel(tr("Frame"), this));
    m_frame = new QSpinBox(this);
    m_frame->setRange(-1000000, 1000000);
    bar->addWidget(m_frame);
    bar->addWidget(new QLabel(tr("FPS"), this));
    m_fps = new QDoubleSpinBox(this);
    m_fps->setRange(0.1, 240.0);
    m_fps->setDecimals(2);
    bar->addWidget(m_fps);
    bar->addWidget(new QLabel(tr("Range"), this));
    m_start = new QSpinBox(this);
    m_end = new QSpinBox(this);
    m_start->setRange(-1000000, 1000000);
    m_end->setRange(-1000000, 1000000);
    bar->addWidget(m_start);
    bar->addWidget(m_end);
    m_loop = new AppCheckBox(tr("Loop"), this);
    m_autoKey = new AppCheckBox(tr("Auto Key"), this);
    bar->addWidget(m_loop);
    bar->addWidget(m_autoKey);
    auto* interpolation = new QComboBox(this);
    interpolation->addItem(tr("Linear"), static_cast<int>(anim::Interpolation::Linear));
    interpolation->addItem(tr("Hold"), static_cast<int>(anim::Interpolation::Hold));
    interpolation->addItem(tr("Bezier"), static_cast<int>(anim::Interpolation::Bezier));
    interpolation->setToolTip(tr("Interpolation for the selected keyframe"));
    bar->addWidget(interpolation);
    auto* applyInterpolation = button(tr("Apply"), tr("Apply interpolation"));
    bar->addWidget(new QLabel(tr("Zoom"), this));
    auto* zoom = new QSlider(Qt::Horizontal, this);
    zoom->setRange(4, 48);
    zoom->setValue(12);
    zoom->setFixedWidth(90);
    zoom->setToolTip(tr("Timeline horizontal zoom"));
    bar->addWidget(zoom);
    bar->addStretch(1);

    auto* addKey = button(tr("+ Key"), tr("Add a keyframe to the selected property"));
    auto* keyLeft = button(QStringLiteral("Key ◀"), tr("Move selected keys one frame left"));
    auto* keyRight = button(QStringLiteral("Key ▶"), tr("Move selected keys one frame right"));
    auto* copyKey = button(tr("Copy"), tr("Copy selected keyframe or cel"));
    auto* pasteKey = button(tr("Paste"), tr("Paste keyframe or cel at current frame"));
    auto* newCel = button(tr("New Cel"), tr("Create a blank raster cel"));
    auto* duplicateCel = button(tr("Duplicate"), tr("Duplicate the active cel"));
    auto* emptyFrame = button(tr("Empty"), tr("Create an explicit empty frame"));
    auto* removeCel = button(tr("Delete"), tr("Remove the selected keyframes or the raster key"));
    root->addLayout(bar);

    m_view = new TimelineView(this);
    root->addWidget(m_view, 1);

    connect(previous, &QPushButton::clicked, this, [this] {
        if (m_controller) m_controller->playbackController()->previousFrame();
    });
    connect(next, &QPushButton::clicked, this, [this] {
        if (m_controller) m_controller->playbackController()->nextFrame();
    });
    connect(m_play, &QPushButton::clicked, this, [this] {
        if (!m_controller) return;
        auto* playback = m_controller->playbackController();
        playback->isPlaying() ? playback->pause() : playback->play();
    });
    connect(stop, &QPushButton::clicked, this, [this] {
        if (m_controller) m_controller->playbackController()->stop();
    });
    connect(m_frame, qOverload<int>(&QSpinBox::valueChanged), this, [this](int frame) {
        if (m_controller) m_controller->playbackController()->goToFrame(frame);
    });
    connect(m_fps, &QDoubleSpinBox::editingFinished, this, [this] {
        if (!m_controller || !m_controller->document()) return;
        const double fps = m_fps->value();
        if (m_controller->document()->animation.fps() == fps) return;
        auto command = std::make_unique<anim::ChangeFrameRateCommand>(
            m_controller->document(), fps);
        command->execute();
        m_controller->history().push(std::move(command));
        refresh();
    });
    auto rangeChanged = [this] {
        if (!m_controller || !m_controller->document()) return;
        auto* doc = m_controller->document();
        if (doc->animation.startFrame() == m_start->value()
            && doc->animation.endFrame() == m_end->value()) return;
        auto command = std::make_unique<anim::ChangeAnimationRangeCommand>(
            doc, m_start->value(), m_end->value(),
            m_start->value(), m_end->value());
        command->execute();
        m_controller->history().push(std::move(command));
        refresh();
    };
    connect(m_start, &QSpinBox::editingFinished, this, rangeChanged);
    connect(m_end, &QSpinBox::editingFinished, this, rangeChanged);
    connect(m_loop, &QCheckBox::toggled, this, [this](bool on) {
        if (m_controller) m_controller->playbackController()->setLoop(on);
    });
    connect(m_autoKey, &QCheckBox::toggled, this, [this](bool on) {
        if (m_controller) m_controller->setAutoKey(on);
    });
    connect(zoom, &QSlider::valueChanged, this, [this](int width) {
        m_view->setFrameWidth(width);
    });
    connect(applyInterpolation, &QPushButton::clicked, this,
            [this, interpolation] {
        if (!m_controller || !m_controller->document()) return;
        const auto row = m_view->currentRow();
        if (!row.valid || row.raster) return;
        auto command = std::make_unique<anim::ChangeKeyframeInterpolationCommand>(
            m_controller->document(), row.layer, row.prop,
            m_controller->currentFrame(),
            static_cast<anim::Interpolation>(interpolation->currentData().toInt()));
        command->execute();
        m_controller->history().push(std::move(command));
        refresh();
    });
    connect(newCel, &QPushButton::clicked, this, [this] {
        if (m_controller && m_controller->createRasterCel(false)) refresh();
    });
    connect(addKey, &QPushButton::clicked, this, [this] {
        if (!m_controller || !m_controller->document()) return;
        const auto row = m_view->currentRow();
        if (!row.valid || row.raster) return;
        LayerTreeNode* node = nullptr;
        for (auto* candidate : m_controller->document()->flatten())
            if (candidate && candidate->id == row.layer) { node = candidate; break; }
        if (!node) return;
        anim::AnimationValue value;
        const auto transform = anim::decomposeTransform(node->transform());
        switch (row.prop) {
        case anim::Property::PositionX: value = anim::AnimationValue(float(transform.position.x())); break;
        case anim::Property::PositionY: value = anim::AnimationValue(float(transform.position.y())); break;
        case anim::Property::ScaleX: value = anim::AnimationValue(float(transform.scale.x())); break;
        case anim::Property::ScaleY: value = anim::AnimationValue(float(transform.scale.y())); break;
        case anim::Property::Rotation: value = anim::AnimationValue(float(transform.rotation)); break;
        case anim::Property::SkewX: value = anim::AnimationValue(float(transform.skew.x())); break;
        case anim::Property::SkewY: value = anim::AnimationValue(float(transform.skew.y())); break;
        case anim::Property::PivotX: value = anim::AnimationValue(float(transform.pivot.x())); break;
        case anim::Property::PivotY: value = anim::AnimationValue(float(transform.pivot.y())); break;
        case anim::Property::Opacity: value = anim::AnimationValue(node->opacity()); break;
        case anim::Property::Visibility: value = anim::AnimationValue(node->isVisible()); break;
        case anim::Property::BlendMode:
            value = anim::AnimationValue::fromEnum(static_cast<int>(node->blendMode())); break;
        }
        auto command = std::make_unique<anim::SetKeyframeCommand>(
            m_controller->document(), row.layer, row.prop,
            m_controller->currentFrame(), value);
        command->execute();
        m_controller->history().push(std::move(command));
        refresh();
    });
    auto moveSelectedKey = [this](int delta) {
        if (!m_controller || !m_controller->document()) return;
        if (m_view->hasKeyframeSelection()) {
            m_view->moveSelection(delta);
            return;
        }
        const auto row = m_view->currentRow();
        if (row.valid && row.raster)
            m_controller->moveRasterCelKeyframe(m_controller->currentFrame() + delta);
        refresh();
    };
    connect(keyLeft, &QPushButton::clicked, this,
            [moveSelectedKey] { moveSelectedKey(-1); });
    connect(keyRight, &QPushButton::clicked, this,
            [moveSelectedKey] { moveSelectedKey(1); });
    connect(copyKey, &QPushButton::clicked, this, [this] {
        if (!m_controller || !m_controller->document()) return;
        const auto row = m_view->currentRow();
        if (!row.valid) return;
        if (!row.raster) {
            const auto* track = m_controller->document()->animation.track(row.layer, row.prop);
            const auto* key = track ? track->keyframeAt(m_controller->currentFrame()) : nullptr;
            if (!key) return;
            m_propertyClipboard = *key;
            m_copiedPropertyKey = true;
            m_copiedRasterKey = false;
        } else {
            auto* node = m_controller->document()->activeNode();
            const auto* track = node
                ? m_controller->document()->animation.rasterTrack(node->id) : nullptr;
            if (!track || !track->hasKeyframe(m_controller->currentFrame())) return;
            const auto celId = track->keyframeCel(m_controller->currentFrame());
            m_rasterClipboard.reset();
            if (celId) {
                if (const auto* content =
                        m_controller->document()->animation.celStorage().content(*celId))
                    m_rasterClipboard = *content;
            }
            m_copiedRasterKey = true;
            m_copiedPropertyKey = false;
        }
    });
    connect(pasteKey, &QPushButton::clicked, this, [this] {
        if (!m_controller || !m_controller->document()) return;
        const auto row = m_view->currentRow();
        if (!row.valid) return;
        if (!row.raster && m_copiedPropertyKey) {
            if (anim::valueType(row.prop) != m_propertyClipboard.value.type()) return;
            m_controller->history().beginMacro(tr("Paste Keyframe"));
            auto set = std::make_unique<anim::SetKeyframeCommand>(
                m_controller->document(), row.layer, row.prop,
                m_controller->currentFrame(), m_propertyClipboard.value);
            set->execute();
            m_controller->history().push(std::move(set));
            auto interp = std::make_unique<anim::ChangeKeyframeInterpolationCommand>(
                m_controller->document(), row.layer, row.prop,
                m_controller->currentFrame(), m_propertyClipboard.interpolation);
            interp->execute();
            m_controller->history().push(std::move(interp));
            m_controller->history().endMacro();
        } else if (row.raster && m_copiedRasterKey) {
            m_controller->pasteRasterCel(m_rasterClipboard);
        }
        refresh();
    });
    connect(duplicateCel, &QPushButton::clicked, this, [this] {
        if (m_controller && m_controller->createRasterCel(true)) refresh();
    });
    connect(emptyFrame, &QPushButton::clicked, this, [this] {
        if (m_controller && m_controller->createEmptyRasterFrame()) refresh();
    });
    connect(removeCel, &QPushButton::clicked, this, [this] {
        if (!m_controller) return;
        if (m_view->hasKeyframeSelection()) {
            m_view->deleteSelection();
            refresh();
            return;
        }
        const auto row = m_view->currentRow();
        if (row.valid && !row.raster) {
            auto command = std::make_unique<anim::RemoveKeyframeCommand>(
                m_controller->document(), row.layer, row.prop,
                m_controller->currentFrame());
            command->execute();
            m_controller->history().push(std::move(command));
        } else {
            m_controller->removeRasterCelKeyframe();
        }
        refresh();
    });
}

void TimelinePanel::setController(ImageController* controller)
{
    for (const auto& connection : m_connections)
        disconnect(connection);
    m_connections.clear();
    m_controller = controller;
    m_view->setController(controller);
    if (m_controller) {
        m_connections.push_back(connect(m_controller, &ImageController::currentFrameChanged,
            this, [this](int frame) {
                QSignalBlocker block(m_frame);
                m_frame->setValue(frame);
            }));
        m_connections.push_back(connect(m_controller, &ImageController::historyChanged,
            this, &TimelinePanel::refresh));
        m_connections.push_back(connect(m_controller, &ImageController::activeLayerChanged,
            this, [this](int) { refresh(); }));
        auto* playback = m_controller->playbackController();
        m_connections.push_back(connect(playback, &anim::PlaybackController::playingChanged,
            this, [this](bool) { updatePlayButton(); }));
    }
    refresh();
}

void TimelinePanel::updatePlayButton()
{
    const bool playing = m_controller
        && m_controller->playbackController()->isPlaying();
    m_play->setText(playing ? QStringLiteral("❚❚") : QStringLiteral("▶"));
}

void TimelinePanel::refresh()
{
    QSignalBlocker b1(m_frame), b2(m_fps), b3(m_start), b4(m_end),
                   b5(m_loop), b6(m_autoKey);
    Document* doc = m_controller ? m_controller->document() : nullptr;
    setEnabled(doc != nullptr);
    if (!doc) {
        m_view->rebuild();
        updatePlayButton();
        return;
    }

    const int start = doc->animation.startFrame();
    const int end = std::max(start, doc->animation.endFrame());
    m_frame->setRange(start, end);
    m_frame->setValue(doc->currentFrame());
    m_fps->setValue(doc->animation.fps());
    m_start->setValue(start);
    m_end->setValue(end);
    m_loop->setChecked(m_controller->playbackController()->loop());
    m_autoKey->setChecked(m_controller->autoKey());
    m_view->rebuild();
    updatePlayButton();
}
