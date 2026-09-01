#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <optional>

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
 * timeout, and a device that can go away mid-call; a local one answers
 * immediately and still answers through the callback, so a caller written
 * against this works either way.
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
 * @brief What a capture came back with.
 *
 * One of the two, never both and never neither: a snapshot the caller may write
 * down, or a reason it has nothing to write. A failure is a string rather than
 * an enumeration because every one of them is something to tell a person and
 * nothing to branch on -- there is no device here, this plugin would not
 * describe itself, the answer did not come back in time.
 */
struct CaptureOutcome {
    std::optional<magda::ExternalPluginSnapshot> snapshot;
    juce::String failure;

    bool ok() const {
        return snapshot.has_value();
    }
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
    virtual ~DeviceControlPlane() = default;

    /// What a capture is answered with, on the message thread.
    using CaptureCallback = std::function<void(CaptureOutcome)>;

    /**
     * @brief Ask the device at @p key what its plugin holds.
     *
     * @p completed is called exactly once, on the message thread, with a
     * snapshot or with a reason there is none. It may be called before this
     * returns -- a local plane has the plugin right here -- and a caller that
     * assumed otherwise would be assuming something only the local case
     * happens to make true.
     */
    virtual void captureState(magda::engine::DeviceKey key, CaptureCallback completed) = 0;
};

/**
 * @brief The plane for plugins running in this process.
 *
 * It owns nothing. Which device is at a key is the runtime's business, so the
 * lookup is handed in: the same arrangement the load path already has with
 * CurrentDeviceLookup, and what keeps this from becoming a second registry
 * disagreeing with the first.
 *
 * A lookup that returns nothing is a failure rather than an empty snapshot. The
 * two are different findings -- a slot with no plugin in it, against a plugin
 * that would not describe itself -- and a caller told the second when the first
 * happened would write a project's plugin state away as absent.
 */
class LocalDeviceControlPlane final : public DeviceControlPlane {
  public:
    /// The device at a key, or null. Null for a key nothing is bound to, which
    /// includes one whose plugin has not finished loading.
    using DeviceLookup = std::function<EngineExternalDevice*(magda::engine::DeviceKey)>;

    explicit LocalDeviceControlPlane(DeviceLookup devices);

    void captureState(magda::engine::DeviceKey key, CaptureCallback completed) override;

  private:
    DeviceLookup devices_;
};

/**
 * @brief Write @p snapshot into @p device, if it is still the device it was
 *        read from.
 *
 * The commit half, and the reason a capture is two steps with only data
 * between them. A snapshot is read from a plugin at one moment and written into
 * a project at another, and in between a slot can have been given a different
 * plugin, or emptied, or the whole runtime can have gone: writing it down then
 * would put one plugin's patch onto another's device, which is the failure the
 * assignment table exists to make impossible.
 *
 * @p request is what PluginAssignments::request() returned for the key when the
 * capture was asked for. Checked rather than trusted, and checked here rather
 * than at each call site, so the rule has one owner in this direction as it
 * does in the other (completeExternalPluginLoad).
 *
 * @return whether anything was written.
 */
bool commitCapturedState(const AssignmentRequest& request,
                         const magda::ExternalPluginSnapshot& snapshot, magda::DeviceInfo& device);

}  // namespace magda::daw::audio::engine_adapter
