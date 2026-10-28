#pragma once

#include "ai/sd/SdGenerationTypes.hpp"

#include <QObject>
#include <QString>
#include <QThreadPool>

// Keeps inference off the GUI thread and reports what it is doing.
//
// One generation at a time per PROCESS, on purpose: a second concurrent run
// would need a second inference context, and the point of SdRuntime is that
// there is exactly one. The limit is enforced across instances rather than per
// instance because more than one of these exists — the experimental Stable
// Diffusion panel and the Neural Flow editor each own an executor, and both are
// reachable at the same time. A request arriving while one is in flight is
// refused with a reason rather than queued; a queue would let a user stack up
// ten multi-minute runs with one impatient click.
//
// This is deliberately not a scheduler. No priorities, no queue, no multi-GPU
// distribution: start, report, cancel, finish.
namespace sdlocal {

class SdGenerationJob : public QObject {
    Q_OBJECT
public:
    explicit SdGenerationJob(QObject* parent = nullptr);
    ~SdGenerationJob() override;

    // Starts a run and returns true. Returns false without starting anything
    // when one is already in flight, filling `refusal` with the reason.
    bool start(const SdGenerationRequest& request, QString* refusal = nullptr);

    // Asks the library to abandon the run as soon as it can. The finished()
    // signal still arrives, with state Cancelled. A cancel during the model
    // load cannot reach the library, so it takes effect when the load returns.
    void cancel();

    bool busy() const { return m_running; }
    SdGenerationState state() const { return m_state; }

    // Cancels and waits for the worker, then releases the inference context.
    // Called from the destructor, so no inference can outlive the Qt objects it
    // posts to and no multi-gigabyte context survives the window that drove it.
    void shutdown();

signals:
    // Always emitted on the thread that owns this object.
    void stateChanged(sdlocal::SdGenerationState state, const QString& detail);
    void progress(int step, int steps, float seconds);
    void finished(const sdlocal::SdGenerationResult& result);

private:
    void setState(SdGenerationState state, const QString& detail = QString());
    // Gives the process-wide generation slot back, if this job is holding it.
    void releaseSlot();

    // One worker thread, and shutdown() waits on it, so the worker's `this`
    // capture can never outlive this object.
    QThreadPool m_pool;
    bool m_running = false;
    // This job is the one holding the process-wide slot. Separate from
    // m_running so a teardown that outruns the queued completion still returns
    // the slot exactly once.
    bool m_holdsSlot = false;
    SdGenerationState m_state = SdGenerationState::Idle;
};

} // namespace sdlocal
