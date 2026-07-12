#include "PlaybackController.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/Document.hpp"

namespace anim {

PlaybackController::PlaybackController(QObject* parent)
    : QObject(parent)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout,
            this, &PlaybackController::onTimer);
}

void PlaybackController::setDocument(Document* document)
{
    if (m_document == document)
        return;

    pause();
    m_document = document;
    m_anchorFrame = m_document ? m_document->currentFrame() : 0;
}

void PlaybackController::setLoop(bool enabled)
{
    if (m_loop == enabled)
        return;
    m_loop = enabled;
    emit loopChanged(m_loop);
}

void PlaybackController::play()
{
    if (!m_document || m_playing)
        return;

    const Range range = effectivePlaybackRange();
    Frame frame = m_document->currentFrame();
    // Starting playback at the end replays the selected range from its start.
    if (frame < range.start || frame >= range.end) {
        frame = range.start;
        requestFrame(frame);
    }

    restartClock(frame);
    setPlaying(true);
}

void PlaybackController::pause()
{
    if (!m_playing)
        return;
    setPlaying(false);
}

void PlaybackController::stop()
{
    // Defined stop rule for the initial backend: preserve the current frame.
    pause();
}

void PlaybackController::goToFrame(int frame)
{
    if (!m_document)
        return;

    const Frame start = m_document->animation.startFrame();
    const Frame end = std::max(start, m_document->animation.endFrame());
    requestFrame(std::clamp(frame, start, end));

    // Manual navigation while playing establishes a new real-time origin so
    // the next timer callback continues from the requested frame.
    if (m_playing)
        restartClock(m_document->currentFrame());
}

void PlaybackController::nextFrame()
{
    if (!m_document)
        return;
    const Frame end = std::max(m_document->animation.startFrame(),
                               m_document->animation.endFrame());
    if (m_document->currentFrame() < end)
        goToFrame(m_document->currentFrame() + 1);
}

void PlaybackController::previousFrame()
{
    if (!m_document)
        return;
    const Frame start = m_document->animation.startFrame();
    if (m_document->currentFrame() > start)
        goToFrame(m_document->currentFrame() - 1);
}

PlaybackController::Range PlaybackController::effectivePlaybackRange() const
{
    if (!m_document)
        return {};

    const Frame documentStart = m_document->animation.startFrame();
    const Frame documentEnd = std::max(documentStart,
                                       m_document->animation.endFrame());
    Range range;
    range.start = std::clamp(m_document->animation.playbackStart(),
                             documentStart, documentEnd);
    range.end = std::clamp(m_document->animation.playbackEnd(),
                           range.start, documentEnd);
    return range;
}

void PlaybackController::requestFrame(Frame frame)
{
    if (!m_document || frame == m_document->currentFrame())
        return;
    emit frameRequested(frame);
}

void PlaybackController::restartClock(Frame anchorFrame)
{
    m_anchorFrame = anchorFrame;
    m_anchorFps = m_document ? m_document->animation.fps() : 24.0;
    m_elapsed.restart();

    // Wake at least twice per source frame, capped at a responsive 16 ms. The
    // callback cadence never determines animation speed; onTimer uses elapsed
    // real time to select the frame.
    const double fps = m_anchorFps;
    const int intervalMs = std::clamp(
        static_cast<int>(std::lround(500.0 / std::max(0.001, fps))), 1, 16);
    m_timer.start(intervalMs);
}

void PlaybackController::setPlaying(bool playing)
{
    if (m_playing == playing)
        return;
    m_playing = playing;
    if (!m_playing)
        m_timer.stop();
    emit playingChanged(m_playing);
}

void PlaybackController::onTimer()
{
    if (!m_playing || !m_document)
        return;

    const Range range = effectivePlaybackRange();
    const double fps = m_document->animation.fps();
    const Frame current = m_document->currentFrame();

    // Range/FPS may be edited by the future timeline while playback is active.
    // Re-anchor instead of retroactively applying the new timing to all elapsed
    // time or continuing from a frame that is no longer in the playback range.
    if (current < range.start || current > range.end) {
        requestFrame(range.start);
        restartClock(range.start);
        return;
    }
    if (std::abs(fps - m_anchorFps) > std::numeric_limits<double>::epsilon()) {
        restartClock(current);
        return;
    }

    const long double elapsedFrames =
        static_cast<long double>(m_elapsed.nsecsElapsed())
        * static_cast<long double>(fps) / 1000000000.0L;
    const qint64 advanced = static_cast<qint64>(std::floor(elapsedFrames));

    if (m_loop) {
        const qint64 frameCount = static_cast<qint64>(range.end)
                                - static_cast<qint64>(range.start) + 1;
        const qint64 fromStart = static_cast<qint64>(m_anchorFrame)
                               - static_cast<qint64>(range.start);
        const Frame expected = static_cast<Frame>(
            static_cast<qint64>(range.start)
            + ((fromStart + advanced) % frameCount + frameCount) % frameCount);
        requestFrame(expected);
        return;
    }

    const qint64 expected = static_cast<qint64>(m_anchorFrame) + advanced;
    if (expected <= range.end) {
        requestFrame(static_cast<Frame>(expected));
        return;
    }

    // Ensure the last frame is displayed once even when a delayed callback
    // jumps beyond it, then finish without changing the current frame again.
    requestFrame(range.end);
    setPlaying(false);
    emit playbackFinished();
}

} // namespace anim
