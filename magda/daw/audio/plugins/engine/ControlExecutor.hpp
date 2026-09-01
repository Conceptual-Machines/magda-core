#pragma once

#include <juce_events/juce_events.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

/**
 * @file ControlExecutor.hpp
 * @brief The one thread everything that is not a block runs on (#2270).
 *
 * A device has two sides. The audio side is a block arriving on whatever thread
 * the host renders from, and it is already serialised by being one callback.
 * The control side -- reading a plugin's state, loading a preset, changing a
 * program, opening an editor -- is not serialised by anything unless something
 * says so, and the plugin underneath it is entitled to assume it is: every one
 * of those operations suspends the plugin, does something to it, and resumes
 * it, so two of them overlapping means the first to finish resumes a plugin the
 * second is still working on.
 *
 * A host with a GUI already has an answer, and it is the message thread. The
 * mistake worth naming is treating the absence of one as an absence of the
 * question: a headless host has whatever threads it has, and "no message
 * manager" is not permission for all of them to reach a plugin at once. So the
 * control side has an execution context of its own, explicitly, and both kinds
 * of host name one rather than one kind going without.
 *
 * ## One at a time, and that includes nesting
 *
 * Work is queued, always, including work asked for from the executor itself.
 * Running a nested piece inline would be the very failure this exists to
 * prevent: an operation that has suspended a plugin and asks for another would
 * have the second run inside it, resume the plugin, and let audio back in
 * halfway through the first. It would also jump the queue past work already
 * waiting.
 *
 * So run() is fire and forget in every case, and composition is by callback:
 * an operation that needs another does it from its own completion, which by
 * then has ended.
 *
 * ## What happens to work that was accepted
 *
 * Accepted work is always called exactly once, and told which of the two it is:
 * it either ran, or it was cancelled because the executor went away before its
 * turn. That is what lets a caller above this keep a promise of its own -- a
 * capture owes its answer exactly once, and an executor that quietly dropped
 * queued work would leave callers waiting for an answer nobody was left to
 * send.
 *
 * Cancelled work runs on whichever thread destroys the executor and must touch
 * nothing but its own reporting. A cancellation is not a late chance to reach a
 * plugin: the reason there is one is that the plugins are going away.
 *
 * Submission itself can fail, and says so rather than pretending: an executor
 * that has stopped accepts nothing, and a message loop that is shutting down
 * refuses a post. A caller that is refused has been told nothing will run, and
 * answers for the operation itself.
 *
 * ## What this is for
 *
 * Every control operation on a device runs here, and every answer to one is
 * delivered here. That is what makes an out-of-process implementation an
 * add-on: its request goes out from this thread and its reply is marshalled
 * back onto the same one, so a caller writing a project's model in a callback
 * is on the thread it was always on, whichever process answered.
 *
 * The capture is the first of those operations (DeviceControl.hpp). Presets,
 * programs and editor windows are the same shape and belong here as they
 * arrive, rather than each finding its own thread and its own convention.
 *
 * The asynchronous load is the one control operation that predates this and
 * still has a contract of its own: it completes on the message thread, which is
 * where JUCE hands a plugin over. It is not wrong, and it is the next thing to
 * join, so that a host with plugins in another process has one thread its
 * device operations happen on rather than two.
 */

namespace magda::daw::audio::engine_adapter {

/// Which of the two things happened to a piece of accepted work.
enum class ExecutionState {
    /// Its turn came and this is it, on the executor's own thread.
    Ran,

    /// The executor went away first. Report and return: the devices this would
    /// have reached are going away too, and this is not running on the control
    /// thread.
    Cancelled,
};

/**
 * @brief Where a device's control operations run.
 *
 * Serial: one piece of work at a time, in the order it was accepted, nesting
 * included. That is the whole contract, and it is what the plugins underneath
 * are entitled to.
 */
class ControlExecutor {
  public:
    virtual ~ControlExecutor() = default;

    ControlExecutor() = default;
    ControlExecutor(const ControlExecutor&) = delete;
    ControlExecutor& operator=(const ControlExecutor&) = delete;
    ControlExecutor(ControlExecutor&&) = delete;
    ControlExecutor& operator=(ControlExecutor&&) = delete;

    using Work = std::function<void(ExecutionState)>;

    /**
     * @brief Queue @p work, to run on this executor in the order it arrived.
     *
     * Never immediate, including when the caller is already on the executor.
     * See the file comment: an operation that ran nested work inline would be
     * letting a second transaction into a plugin the first has open.
     *
     * @return whether it was accepted. Accepted work is called exactly once,
     *         with Ran or with Cancelled. A refusal means nothing will run it
     *         and nothing will call it, so whatever the caller owed for it is
     *         still owed.
     */
    virtual bool run(Work work) = 0;

    /// Whether the calling thread is this executor's own.
    virtual bool isCurrent() const = 0;
};

/**
 * @brief The executor for a host that has a message thread.
 *
 * Which is every host with a window in it. Control work belongs on the message
 * thread there for reasons older than this file: it is the thread a plugin's
 * editor lives on, the one its own host callbacks expect, and the one the model
 * a callback writes into is edited from.
 *
 * The one guarantee it cannot make is the one JUCE does not: a message posted
 * and then abandoned because the loop itself was torn down is a call that never
 * arrives. That is process teardown rather than a plane closing, and it is
 * stated here rather than papered over.
 */
class MessageThreadControlExecutor final : public ControlExecutor {
  public:
    bool run(Work work) override;
    bool isCurrent() const override;
};

/**
 * @brief The executor for a host that has no message thread.
 *
 * An offline render, a command-line tool, a test binary. It owns a thread and
 * runs what it is given on that one, in order, which is the same guarantee the
 * message thread gives an application and the reason a headless host is not
 * left deciding per call site.
 *
 * Work still queued when this is destroyed is cancelled rather than dropped, so
 * every accepted operation is still answered once. Cancellation runs on the
 * thread doing the destroying, before it waits for the worker.
 *
 * ## Being destroyed by its own work
 *
 * A completion running here is entitled to close the thing that owns this
 * executor -- a project closing from a callback is an ordinary outcome -- and
 * that makes the last reference fall on the executor's own thread. So the state
 * the worker touches is held apart from this object and outlives it, and a
 * destructor that finds itself on the worker detaches rather than joining: a
 * thread cannot wait for itself, and a worker whose state is still alive can be
 * left to finish the block it is in and stop.
 */
class SerialControlThread final : public ControlExecutor {
  public:
    SerialControlThread();
    ~SerialControlThread() override;

    bool run(Work work) override;
    bool isCurrent() const override;

  private:
    /// Everything the worker touches, owned by the worker as much as by the
    /// executor, so that either can be the last to let go.
    struct Shared {
        std::mutex lock;
        std::condition_variable wake;
        std::deque<Work> queued;
        bool stopping = false;
    };

    std::shared_ptr<Shared> shared_;
    std::thread thread_;
};

}  // namespace magda::daw::audio::engine_adapter
