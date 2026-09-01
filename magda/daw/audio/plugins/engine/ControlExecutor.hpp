#pragma once

#include <juce_events/juce_events.h>

#include <condition_variable>
#include <deque>
#include <functional>
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

/**
 * @brief Where a device's control operations run.
 *
 * Serial: one piece of work at a time, in the order it was given. That is the
 * whole contract, and it is what the plugins underneath are entitled to.
 */
class ControlExecutor {
  public:
    virtual ~ControlExecutor() = default;

    ControlExecutor() = default;
    ControlExecutor(const ControlExecutor&) = delete;
    ControlExecutor& operator=(const ControlExecutor&) = delete;
    ControlExecutor(ControlExecutor&&) = delete;
    ControlExecutor& operator=(ControlExecutor&&) = delete;

    using Work = std::function<void()>;

    /**
     * @brief Run @p work on this executor.
     *
     * Immediately when the caller is already on it, which is what keeps an
     * operation asked for from inside another one from waiting on a thread it
     * is standing on; queued otherwise.
     */
    virtual void run(Work work) = 0;

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
 */
class MessageThreadControlExecutor final : public ControlExecutor {
  public:
    void run(Work work) override;
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
 * Work queued but not yet run is dropped when this is destroyed. That is the
 * ordering rule it exists under and the reason it is safe: an executor is
 * destroyed before the devices whose plugins its work would have reached, so
 * work that has not started must not start, and whoever was waiting for the
 * answer is the thing being torn down.
 */
class SerialControlThread final : public ControlExecutor {
  public:
    SerialControlThread();
    ~SerialControlThread() override;

    void run(Work work) override;
    bool isCurrent() const override;

  private:
    void loop();

    std::mutex lock_;
    std::condition_variable wake_;
    std::deque<Work> queued_;
    bool stopping_ = false;

    /// Last, so the thread starts once everything it reads is built.
    std::thread thread_;
};

}  // namespace magda::daw::audio::engine_adapter
