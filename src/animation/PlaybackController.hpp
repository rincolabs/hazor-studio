#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QTimer>

#include "AnimationTypes.hpp"

class Document;

namespace anim {

// Time-based animation playback backend. The timer is only a wake-up source:
// the displayed frame is always derived from real elapsed time and the
// document FPS, so delayed callbacks skip preview frames instead of slowing the
// animation down. Frame evaluation/rendering remains centralized in
// ImageController through frameRequested().
class PlaybackController final : public QObject {
    Q_OBJECT

public:
    explicit PlaybackController(QObject* parent = nullptr);

    void setDocument(Document* document);
    Document* document() const { return m_document; }

    bool isPlaying() const { return m_playing; }
    bool loop() const { return m_loop; }
    void setLoop(bool enabled);

public slots:
    void play();
    void pause();

    // Stop preserves the currently displayed frame. It only ends playback and
    // never writes base layer values.
    void stop();

    void goToFrame(int frame);
    void nextFrame();
    void previousFrame();

signals:
    // ImageController connects this to its central setCurrentFrame() operation,
    // keeping evaluation, GPU sync and repaint on the existing render path.
    void frameRequested(int frame);
    void playingChanged(bool playing);
    void loopChanged(bool enabled);
    void playbackFinished();

private slots:
    void onTimer();

private:
    struct Range {
        Frame start = 0;
        Frame end = 0;
    };

    Range effectivePlaybackRange() const;
    void requestFrame(Frame frame);
    void restartClock(Frame anchorFrame);
    void setPlaying(bool playing);

    Document* m_document = nullptr;
    QTimer m_timer;
    QElapsedTimer m_elapsed;
    Frame m_anchorFrame = 0;
    double m_anchorFps = 24.0;
    bool m_playing = false;
    bool m_loop = false;
};

} // namespace anim
