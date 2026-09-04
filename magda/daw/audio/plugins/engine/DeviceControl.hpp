#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>
#include <optional>

#include "ControlExecutor.hpp"
#include "PluginAssignments.hpp"
#include "plan/RenderPlan.hpp"
#include "plugin_manager/ExternalPluginState.hpp"

/**
 * @file DeviceControl.hpp
 * @brief Everything that is not rendering, addressed the way rendering is (#2270).
 *
 * #1893 asks for location transparency from day one, so running a plugin in
 * another process later is an add-on rather than a rework. The realtime half
 * already has it (EngineDevice says nothing about where a device lives); the
 * control half did not, since editor windows and state captures reached the
 * plugin by asking for its juce::AudioPluginInstance directly -- the one
 * thing a plugin in another process can't hand over.
 *
 * This is an endpoint rather than an accessor because a bare instance pointer
 * is also how the "reading state and rendering must not overlap" rule loses
 * its owner: #2268 found two races of the same shape, where control and audio
 * both reached the plugin and neither side owned the rule. Behind an
 * endpoint, only the object owning the instance can touch it, from either
 * side.
 *
 * Keyed by magda::engine::DeviceKey rather than a live reference. Async and
 * fallible, since a remote implementation has IPC, a timeout, and a device
 * that can go away mid-call. Everything runs and answers on the plane's
 * ControlExecutor (ControlExecutor.hpp) -- one executor per plane serializes
 * every operation against every other, including reads that suspend/resume a
 * plugin, which is what actually needs serializing and can't be done by
 * checking a thread on a headless host.
 *
 * Nothing here writes a model directly: a capture comes back as data, the
 * caller checks the assignment it was read from still holds, then writes it
 * -- the same boundary completeExternalPluginLoad() applies on the load path,
 * for the same reason (a slot can be given a different plugin, or none,
 * between asking and answering).
 *
 * Not here yet: the editor window, MAGDA's other caller that reaches for an
 * instance. It belongs here when it's ported off the fork's plugin windows --
 * an editor is a control-plane request like any other.
 */

namespace magda::daw::audio::engine_adapter {

class EngineExternalDevice;

/**
 * @brief What a capture came back with: a snapshot, or a reason there is none.
 *
 * Exactly one of the two, enforced by the type rather than a comment -- this
 * matters most at the boundary this is built for, where an out-of-process
 * implementation decodes one of these from bytes and a both-or-neither
 * aggregate would read a "success with an ignored reason" as fine.
 *
 * A failure is a string, not an enum: every one of them is something to tell
 * a person (no device here, plugin wouldn't describe itself, timed out), not
 * to branch on.
 */
class CaptureOutcome {
  public:
    /// What the plugin held.
    static CaptureOutcome taken(magda::ExternalPluginSnapshot snapshot);

    /// Why it did not.
    static CaptureOutcome failed(juce::String reason);

    bool ok() const {
        return snapshot_.has_value();
    }

    /// The snapshot. Only when ok().
    const magda::ExternalPluginSnapshot& snapshot() const;

    /// Why there is none. Empty when ok().
    const juce::String& failure() const {
        return failure_;
    }

  private:
    CaptureOutcome() = default;

    std::optional<magda::ExternalPluginSnapshot> snapshot_;
    juce::String failure_;
};

/**
 * @brief Where a host asks a device for anything that is not a block.
 *
 * One implementation runs the plugin in this process; another asks a process
 * that is not this one. Which a host holds is the whole difference between
 * an in-process and a sandboxed plugin, and nothing above this needs to know
 * which (#1899).
 */
class DeviceControlPlane {
  public:
    /// @p executor is where this plane's work runs and its answers arrive.
    /// Held by shared_ptr because a remote implementation's reply comes back
    /// long after the call that sent it.
    explicit DeviceControlPlane(std::shared_ptr<ControlExecutor> executor);

    virtual ~DeviceControlPlane() = default;

    DeviceControlPlane(const DeviceControlPlane&) = delete;
    DeviceControlPlane& operator=(const DeviceControlPlane&) = delete;
    DeviceControlPlane(DeviceControlPlane&&) = delete;
    DeviceControlPlane& operator=(DeviceControlPlane&&) = delete;

    /// What a capture is answered with, on this plane's executor.
    using CaptureCallback = std::function<void(CaptureOutcome)>;

    /**
     * @brief Ask the device at @p key what its plugin holds.
     *
     * Asked from any thread, answered on the executor, always later than
     * this returns -- so a caller writing a project's model in the callback
     * is always on the thread it's entitled to write from.
     *
     * @return whether the request was accepted. False means the plane is
     *         closing: @p completed will not be called, and the caller is
     *         told so synchronously rather than by a callback arriving from
     *         an unexpected thread.
     *
     * An accepted request calls @p completed exactly once, on the executor,
     * with either the capture or a failure saying the plane closed first.
     * The callback outlives the call: whatever it captures must still be
     * there when it runs, or held weakly and checked.
     */
    virtual bool captureState(magda::engine::DeviceKey key, CaptureCallback completed) = 0;

    /// Where this plane's work runs, for a host that has something else to
    /// put on the same thread.
    const std::shared_ptr<ControlExecutor>& executor() const {
        return executor_;
    }

  private:
    std::shared_ptr<ControlExecutor> executor_;
};

/**
 * @brief The devices a plane can reach, owned by whoever runs them (#2270).
 *
 * A plane's work runs later than the call that queued it, so between the two
 * a project can close and a device can leave the chain it was in. The
 * registry itself is held weakly (a registry that won't lock is a runtime
 * that's gone); what find() returns is a lease that carries the device
 * rather than pointing at it, so a device removed or destroyed mid-capture
 * is one the capture is still holding. Same shape the load path already
 * uses for the same problem (PluginAssignments.hpp).
 */
class DeviceRegistry {
  public:
    virtual ~DeviceRegistry() = default;

    DeviceRegistry() = default;
    DeviceRegistry(const DeviceRegistry&) = delete;
    DeviceRegistry& operator=(const DeviceRegistry&) = delete;
    DeviceRegistry(DeviceRegistry&&) = delete;
    DeviceRegistry& operator=(DeviceRegistry&&) = delete;

    /**
     * @brief A lease on the device at @p key, or nothing.
     *
     * A lease rather than a bare pointer: holding the registry alive only
     * says the registry is alive, not that a given device wasn't removed or
     * destroyed from under it. The lifetime comes back with the answer, and
     * the operation holds it until finished -- which is why a runtime owns
     * its external devices by shared_ptr. Empty for a key nothing is bound
     * to (including one still loading). Called on the control executor.
     */
    virtual std::shared_ptr<EngineExternalDevice> find(magda::engine::DeviceKey key) const = 0;
};

/**
 * @brief The plane for plugins running in this process.
 *
 * Owns nothing: the runtime's registry is held weakly, so a project closing
 * between a request and its turn is a failure with a reason rather than a
 * dereference of something gone.
 */
class LocalDeviceControlPlane final : public DeviceControlPlane {
  public:
    LocalDeviceControlPlane(std::shared_ptr<ControlExecutor> executor,
                            std::weak_ptr<const DeviceRegistry> devices);

    bool captureState(magda::engine::DeviceKey key, CaptureCallback completed) override;

  private:
    std::weak_ptr<const DeviceRegistry> devices_;
};

/**
 * @brief The device a key names now, to write onto.
 *
 * The write-side twin of CurrentDeviceLookup on the load path: asked at
 * completion, not when the capture was requested, so a model that changed in
 * between isn't clobbered. Null for a key whose device is gone, which is a
 * commit that does not happen.
 */
using MutableDeviceLookup = std::function<magda::DeviceInfo*(magda::engine::DeviceKey)>;

/**
 * @brief Write @p snapshot onto the device @p request was made for, if that
 *        is still the device it was read from.
 *
 * The commit half of a capture: between reading a snapshot and writing it, a
 * slot can have been given a different plugin, emptied, or the runtime can
 * be gone -- writing anyway would put one plugin's patch onto another's
 * device. The device is resolved here, from @p request's own key through
 * @p device, rather than accepted as a separate argument, so a caller can't
 * validate one device and write another. @p request is what
 * PluginAssignments::request() returned when the capture was asked for, and
 * is checked here rather than at each call site (same owner as the
 * equivalent check in completeExternalPluginLoad).
 *
 * @return whether anything was written.
 */
bool commitCapturedState(const AssignmentRequest& request,
                         const magda::ExternalPluginSnapshot& snapshot,
                         const MutableDeviceLookup& device);

}  // namespace magda::daw::audio::engine_adapter
