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
 * @brief Everything that is not rendering, addressed the way rendering is
 *        (#2270).
 *
 * #1893 asks for one thing beyond parity: the device op contract is
 * location-transparent from day one, so that running a plugin in another
 * process later is an add-on rather than a rework. The realtime half already
 * honours it -- magda::engine::EngineDevice says nothing about where a device
 * lives, and EngineExternalDevice is one implementation of it -- and the
 * control half did not. Editor windows and state captures reached the plugin by
 * asking the adapter for its juce::AudioPluginInstance, which is the one thing
 * a plugin in another process cannot hand over.
 *
 * ## Why it is an endpoint rather than an accessor
 *
 * A pointer to an instance is not only unavailable across a process boundary;
 * it is also how the serialisation rule lost its owner. Reading a plugin's
 * state and rendering through it must not overlap, and that rule needs one
 * object that can enforce it. Two races in the #2268 review were the same shape
 * twice: an operation reached the plugin from the control side while the audio
 * side was reaching it too, and neither side owned the rule. Behind an endpoint
 * the question does not arise -- the object that owns the instance is the only
 * thing that can touch it, from either side.
 *
 * ## The shape
 *
 * Keyed by magda::engine::DeviceKey, which is the identity the plan and the
 * assignment table already use, rather than by a reference to a live object.
 * Asynchronous and fallible, because a remote implementation has IPC, a
 * timeout, and a device that can go away mid-call.
 *
 * ## Where it all runs
 *
 * On the plane's ControlExecutor, which is the part that has to be explicit
 * rather than assumed (ControlExecutor.hpp). Every operation runs there and
 * every answer is delivered there: a caller may ask from any thread, and what
 * it gets back arrives on the one thread the control side of a device is
 * allowed to be on.
 *
 * That is what serialises these. Reading a plugin's state suspends it and
 * resumes it afterwards, so two operations overlapping would have the first to
 * finish resuming a plugin the second is still inside -- and neither the
 * endpoint nor the device can prevent it by checking a thread, because a
 * headless host has whatever threads it has. One executor answers it for every
 * operation at once, including the ones that are not written yet.
 *
 * Nothing here reaches a model. A capture comes back as data, the caller checks
 * that the assignment it was read from still holds, and only then is it written
 * down -- the same boundary completeExternalPluginLoad() applies to the other
 * direction, and for the same reason: between asking and answering, a slot can
 * have been given a different plugin, or none.
 *
 * ## What is not here yet
 *
 * The editor window. It is the other caller that reaches for an instance, and
 * it does not exist on this path yet: MAGDA's plugin windows are still the
 * fork's. It belongs here when it arrives -- an editor is a control-plane
 * request like any other -- and this file is where it goes rather than a second
 * accessor next to it.
 */

namespace magda::daw::audio::engine_adapter {

class EngineExternalDevice;

/**
 * @brief What a capture came back with: a snapshot, or a reason there is none.
 *
 * Exactly one of the two, and the type is what says so rather than a sentence
 * in a comment. That matters most at the boundary this is being built for: an
 * out-of-process implementation decodes one of these from bytes, and an
 * aggregate that permitted both or neither would leave every decoder
 * reconstructing an invariant nothing enforced -- with the both-present case
 * reading as success and carrying a reason nobody would look at.
 *
 * A failure is a string rather than an enumeration because every one of them is
 * something to tell a person and nothing to branch on: there is no device here,
 * this plugin would not describe itself, the answer did not come back in time.
 * A failure with nothing to say is refused the same way an empty snapshot is,
 * and gets a generic reason instead of an empty one.
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

    /// The snapshot. Only when ok(); a caller that asks otherwise is asking for
    /// state that was never read.
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
 * One implementation runs the plugin in this process and answers out of its own
 * table; another asks a process that is not this one. Which of them a host
 * holds is the whole of the difference between an in-process plugin and a
 * sandboxed one, and nothing above this has to know which it is (#1899).
 */
class DeviceControlPlane {
  public:
    /// @p executor is where this plane's work runs and its answers arrive. Held
    /// by shared_ptr because a remote implementation's reply comes back long
    /// after the call that sent it.
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
     * Asked from any thread, answered on the executor, always later than this
     * returns. A caller writing a project's model in that callback is therefore
     * on the thread it is entitled to write one from, whichever thread asked
     * and whichever process answered -- and it is never handed that callback
     * anywhere else, which is what keeps an isCurrent() check and a hop of its
     * own out of every consumer.
     *
     * @return whether the request was accepted. False is a plane that is
     *         closing: nothing will run and @p completed will not be called, so
     *         the caller still owes whatever it owed and has been told so here,
     *         synchronously, rather than by a callback arriving from a thread
     *         it was not expecting one on.
     *
     * An accepted request calls @p completed exactly once, on the executor,
     * either with what the capture found or with a failure saying the plane
     * closed before its turn came. The one thing that can take an accepted
     * request away without a word is the host's message loop being torn down
     * under it, which is process teardown (ControlExecutor.hpp).
     *
     * The callback outlives the call. Whatever it captures must still be there
     * when it runs, or must be held weakly and checked -- the plane keeps its
     * own end of that with the registry below and cannot keep the caller's.
     */
    virtual bool captureState(magda::engine::DeviceKey key, CaptureCallback completed) = 0;

    /// Where this plane's work runs, for a host that has something else to put
    /// on the same thread.
    const std::shared_ptr<ControlExecutor>& executor() const {
        return executor_;
    }

  private:
    std::shared_ptr<ControlExecutor> executor_;
};

/**
 * @brief A registry that finds nothing is a failure, not an empty snapshot.
 *
 * Worth saying where the two meet: a slot with no plugin in it and a plugin
 * that would not describe itself are different findings, and a caller told the
 * second when the first happened would write a project's plugin state away as
 * absent.
 */

/**
 * @brief The devices a plane can reach, owned by whoever runs them (#2270).
 *
 * A plane's work runs later than the call that queued it, so between the two a
 * project can be closed. What it needs at that moment is not a token beside a
 * lookup -- keeping one alive says nothing about what the other captured -- but
 * the thing that owns the devices, held weakly and locked before it is asked.
 *
 * So the lookup is a type rather than a function, and the plane holds a weak
 * reference to it. Locking it is what keeps the devices alive for the length of
 * the operation, and failing to lock it is the answer that there is nothing
 * left to ask. The relationship is the type's rather than a caller's to
 * maintain: there is no way to hand over a lease that has nothing to do with
 * the devices it is supposed to be keeping.
 *
 * The same shape the load path already has for the same problem, where an
 * assignment is held weakly and a completion that finds it gone is refused
 * (PluginAssignments.hpp).
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
     * @brief The device at @p key, or null.
     *
     * Null for a key nothing is bound to, which includes one whose plugin has
     * not finished loading. Called on the control executor, with the registry
     * held alive for the length of the operation, so what it returns is safe to
     * use until that operation ends.
     */
    virtual EngineExternalDevice* find(magda::engine::DeviceKey key) const = 0;
};

/**
 * @brief The plane for plugins running in this process.
 *
 * It owns nothing: which device is at a key is the runtime's business, and the
 * runtime's registry is held weakly so that a project closing between a request
 * and its turn is a failure with a reason rather than a dereference of
 * something that has gone.
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
 * The same question CurrentDeviceLookup asks on the load path, asked for
 * writing rather than for reading, and asked at the same moment: at completion
 * rather than when the operation started. A model captured when the capture was
 * requested would be a model whatever happened since has already left behind.
 *
 * Null for a key whose device is gone, which is a commit that does not happen.
 */
using MutableDeviceLookup = std::function<magda::DeviceInfo*(magda::engine::DeviceKey)>;

/**
 * @brief Write @p snapshot onto the device @p request was made for, if that is
 *        still the device it was read from.
 *
 * The commit half, and the reason a capture is two steps with only data
 * between them. A snapshot is read from a plugin at one moment and written into
 * a project at another, and in between a slot can have been given a different
 * plugin, or emptied, or the whole runtime can have gone: writing it down then
 * would put one plugin's patch onto another's device, which is the failure the
 * assignment table exists to make impossible.
 *
 * The device is resolved here, from @p request's own key through @p device,
 * rather than passed in beside the token. A caller that handed over both would
 * be free to hand over a model the token says nothing about -- the check would
 * pass, the write would land somewhere else, and the boundary would be a
 * gesture. Validating one thing and writing another is the failure this exists
 * to prevent, so only one of them is the caller's to choose.
 *
 * @p request is what PluginAssignments::request() returned for the key when the
 * capture was asked for. Checked rather than trusted, and checked here rather
 * than at each call site, so the rule has one owner in this direction as it
 * does in the other (completeExternalPluginLoad).
 *
 * @return whether anything was written.
 */
bool commitCapturedState(const AssignmentRequest& request,
                         const magda::ExternalPluginSnapshot& snapshot,
                         const MutableDeviceLookup& device);

}  // namespace magda::daw::audio::engine_adapter
