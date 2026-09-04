#pragma once

#include <juce_events/juce_events.h>

#include <atomic>
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
 * A device has two sides. The audio side is a block arriving on whatever
 * thread the host renders from, serialised by being one callback. The
 * control side -- reading a plugin's state, loading a preset, changing a
 * program, opening an editor -- isn't serialised by anything unless
 * something says so, yet the plugin underneath is entitled to assume it is:
 * every one of those operations suspends the plugin, does something to it,
 * and resumes it, so two overlapping means the first to finish resumes a
 * plugin the second is still working on.
 *
 * A host with a GUI already has an answer: the message thread. A headless
 * host has whatever threads it has, and "no message manager" is not
 * permission for all of them to reach a plugin at once -- so the control
 * side gets an execution context of its own, explicitly, in every host.
 *
 * ## One at a time, including nesting
 *
 * Work is queued, always, including work asked for from the executor
 * itself. Running a nested piece inline would be the failure this exists to
 * prevent: an operation that suspended a plugin and asks for another would
 * have the second run inside it, resume the plugin, and let audio back in
 * halfway through the first, while also jumping the queue past work already
 * waiting. So run() is fire and forget in every case, and composition is by
 * callback: an operation needing another does it from its own completion,
 * which by then has ended.
 *
 * ## What happens to accepted work
 *
 * Accepted work is called exactly once, on the executor, and told which of
 * two things happened: it ran, or it was cancelled because the executor
 * went away before its turn came. Both arrive on the executor's own
 * thread -- the whole point of having one -- so a caller writing a
 * project's model in a completion is entitled to that thread either way,
 * with no isCurrent() check or extra hop needed in every consumer.
 *
 * A cancellation is not a late chance to reach a plugin; the device is
 * going away, and this just lets whoever was waiting stop waiting.
 *
 * Submission itself can fail: a stopped executor accepts nothing, and says
 * so synchronously rather than pretending. A refused caller still owes
 * whatever it owed for that operation, on its own thread, with no callback
 * arriving later to say so.
 *
 * ## Where the guarantee is scoped
 *
 * "Exactly once, on the executor" holds for as long as the executor has a
 * context to run on. A SerialControlThread owns its thread and owns the
 * guarantee outright. A MessageThreadControlExecutor borrows the host's
 * message loop: work it posted is cancelled on that loop when the executor
 * is destroyed, but a loop torn down under a posted call takes the call
 * with it -- that's process teardown rather than a project closing, and is
 * noted here rather than left as a surprise.
 *
 * ## What runs here
 *
 * Every control operation on a device runs here, and every answer is
 * delivered here, which is what makes an out-of-process implementation an
 * add-on: its request goes out from this thread and its reply is
 * marshalled back onto the same one, so a caller writing a project's model
 * in a callback is on the thread it was always on, whichever process
 * answered. The capture is the first such operation (DeviceControl.hpp);
 * presets, programs and editor windows are the same shape and belong here
 * as they arrive.
 *
 * The asynchronous plugin load predates this file and keeps its own
 * contract: it completes on the message thread, where JUCE hands a plugin
 * over. That's the next one to fold in, so a host with plugins in another
 * process ends up with one thread for all of its device operations.
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
     * See the file comment: an operation that ran nested work inline would
     * be letting a second transaction into a plugin the first has open.
     *
     * @return whether it was accepted. Accepted work is called exactly once
     *         and on this executor, with Ran or Cancelled. A refusal means
     *         nothing will run it and nothing will call it, so whatever the
     *         caller owed for it is still owed, on the caller's own thread.
     */
    virtual bool run(Work work) = 0;

    /// Whether the calling thread is this executor's own.
    virtual bool isCurrent() const = 0;
};

/**
 * @brief The executor for a host that has a message thread.
 *
 * Every host with a window in it. Control work belongs on the message
 * thread there for reasons older than this file: it's the thread a
 * plugin's editor lives on, the one its host callbacks expect, and the one
 * the model a callback writes into is edited from.
 *
 * Work already posted when this executor is destroyed is cancelled rather
 * than left to run against a plane that's gone -- the post still arrives on
 * the message thread and is told it was cancelled, keeping the answer on
 * the executor either way.
 *
 * The loop is the host's, not this object's, which is where the base
 * guarantee is scoped rather than kept: a message loop torn down under a
 * posted call takes the call with it, with no thread left to say so on.
 */
class MessageThreadControlExecutor final : public ControlExecutor {
  public:
    MessageThreadControlExecutor();
    ~MessageThreadControlExecutor() override;

    bool run(Work work) override;
    bool isCurrent() const override;

  private:
    /// Shared with every post this executor has made, so that destroying it
    /// turns them all into cancellations rather than calls into a plane that
    /// has gone.
    struct Posted {
        std::atomic<bool> cancelled{false};
    };

    std::shared_ptr<Posted> posted_;
};

/**
 * @brief The executor for a host that has no message thread.
 *
 * An offline render, a command-line tool, a test binary. It owns a thread
 * and runs what it's given on that one, in order -- the same guarantee the
 * message thread gives an application, so a headless host isn't left
 * deciding per call site.
 *
 * Work still queued when this is destroyed is cancelled rather than
 * dropped, so every accepted operation is answered once, on the worker
 * itself, which drains what's left as cancellations before stopping. The
 * destructor waits for that.
 *
 * A completion running here is entitled to close the thing that owns this
 * executor (a project closing from a callback is ordinary), which puts the
 * last reference on the executor's own thread. So the state the worker
 * touches is held apart from this object and outlives it, and a destructor
 * that finds itself on the worker detaches rather than joining -- a thread
 * can't wait for itself, and a worker whose state is still alive can finish
 * the block it's in and stop on its own.
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
