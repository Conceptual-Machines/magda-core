#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>
#include <optional>

#include "core/DeviceInfo.hpp"
#include "exec/EngineDevice.hpp"
#include "exec/RenderContext.hpp"
#include "plan/RenderPlan.hpp"
#include "plugin_manager/ExternalPluginLookup.hpp"
#include "plugin_manager/ExternalPluginState.hpp"
#include "plugins/engine/PluginAssignments.hpp"

/**
 * @file EngineDeviceFactory.hpp
 * @brief What a Device op resolves to, asked of the app's own catalogs (#2174).
 *
 * Internal devices ported to the SDK, plus external plugins (#2243) loaded
 * sync or async. See docs/architecture/engine-device-factory.md for the full
 * contract, including the async-load identity-boundary and lifetime rules.
 */

namespace magda::daw::audio::engine_adapter {

/**
 * @brief The engine device @p device names, or null when nothing can make one.
 *
 * Null for a device whose id is in neither catalog, or that hasn't moved to
 * the SDK yet. @p offlineRender selects what the device is told about
 * isRendering (see EngineMagdaDevice's constructor); defaults to a live one.
 */
std::unique_ptr<magda::engine::EngineDevice> createEngineDevice(const magda::DeviceInfo& device,
                                                                bool offlineRender = false);

/** @brief Whether @p pluginId names a device the engine can build right now. */
bool canCreateEngineDevice(const juce::String& pluginId);

/**
 * @brief Whether either catalog knows @p pluginId at all.
 *
 * True with canCreateEngineDevice() false is the case worth reporting: the
 * app knows the device, the engine can't build it.
 */
bool isRegisteredDevice(const juce::String& pluginId);

/**
 * @brief What a host hands over before the engine can make an external plugin.
 *
 * The formats this build can load and the last scan's results, both
 * app-owned. `context` is only what a plugin allocating at construction goes
 * on; the adapter prepares it for real afterward.
 */
struct ExternalPluginServices {
    juce::AudioPluginFormatManager* formats = nullptr;
    const juce::KnownPluginList* knownPlugins = nullptr;
    magda::engine::RenderContext context{};
};

/**
 * @brief The exact installed plugin and the model facts safe to compile from.
 *
 * A transient copy: resolution never rekeys or mutates the persistent model.
 * Live channel topology is only known after JUCE instantiates the plugin and
 * enables its buses.
 */
struct ExternalPluginResolution {
    magda::DeviceInfo planDevice;
    juce::PluginDescription description;
    juce::String failure;

    explicit operator bool() const {
        return failure.isEmpty();
    }
};

/** @brief Resolve once for both plan compilation and plugin creation. */
ExternalPluginResolution resolveEngineExternalPlugin(const magda::DeviceInfo& device,
                                                     const ExternalPluginServices& services);

/**
 * @brief An external device, or why there is not one.
 *
 * Exactly one of the two is set; `failure` is a message for a person.
 */
struct ExternalDeviceResult {
    std::unique_ptr<magda::engine::EngineDevice> device;
    juce::String failure;

    /**
     * @brief What the plugin holds now, for the caller to write into its model.
     *
     * The chunk wins over the saved parameter array; every downstream read
     * (automation base value, knob position, render writes) comes from the
     * model, so applying it is the caller's job, on the message thread
     * (magda::applyRestoredParameters). Empty when nothing was created.
     */
    std::vector<magda::RestoredParameter> restoredParameters;

    /// The assignment enriched from the live instance after all buses were
    /// enabled. Publish only on success; on failure leave the model as-is.
    std::optional<magda::DeviceInfo> resolvedDevice;
};

/**
 * @brief The adapter over an instance the host made itself.
 *
 * One transaction: enables buses, applies the saved parameter array then the
 * chunk (in that order), and returns what the plugin holds afterward for the
 * caller's model (ExternalPluginState.hpp).
 */
ExternalDeviceResult adaptExternalPluginInstance(
    std::unique_ptr<juce::AudioPluginInstance> instance, const magda::DeviceInfo& device,
    bool offlineRender = false);

/**
 * @brief The external plugin @p device names, created and ready to bind.
 *
 * Blocking: suits a render with nothing else to do meanwhile (offline
 * bounce, corpus case). A session opening a project should use the async
 * form below instead.
 */
ExternalDeviceResult createEngineExternalDevice(const magda::DeviceInfo& device,
                                                const ExternalPluginServices& services,
                                                bool offlineRender = false);

/**
 * @brief What the model says about this device now, asked on the message thread.
 *
 * Read at completion rather than captured at request time, so a slow load
 * can't clobber edits made while it was in flight. Null means the device is
 * gone. Keyed on engine::DeviceKey rather than a bare DeviceId, since
 * DeviceId is allocated per section and a bare id can name up to three
 * devices.
 */
using CurrentDeviceLookup = std::function<const magda::DeviceInfo*(magda::engine::DeviceKey)>;

/**
 * @brief What an asynchronous load remembers about what it asked for.
 *
 * A self-contained, weak-reference snapshot (PluginAssignments.hpp). Saved
 * plugin metadata is deliberately excluded: resolving a moved plugin may
 * legitimately correct it before completion.
 */
struct RequestedPlugin {
    AssignmentRequest assignment;
    juce::String displayName;
    bool resolvedIsInstrument = false;
};

/**
 * @brief The end of an asynchronous load, once the instance exists.
 *
 * Enforces an identity boundary: completes only onto the assignment it was
 * requested for, refusing if that assignment was deleted, replaced,
 * duplicated/pasted from, had its id reused, was never registered, or its
 * runtime is gone. @p error is what the loader reported, used only when
 * @p instance is null. See docs/architecture/engine-device-factory.md.
 */
ExternalDeviceResult completeExternalPluginLoad(std::unique_ptr<juce::AudioPluginInstance> instance,
                                                const juce::String& error,
                                                const RequestedPlugin& requested,
                                                const CurrentDeviceLookup& currentDevice,
                                                bool offlineRender = false);

/**
 * @brief The same as createEngineExternalDevice, without waiting for it.
 *
 * @p completed runs on the message thread when the plugin is ready or has
 * failed; until then the op passes audio through per the executor's existing
 * diagnostics, and completion triggers a re-prepare rather than a hot swap.
 * The saved @p device is read once to decide what to load; @p currentDevice
 * is read again at completion to decide what to restore. @p key and
 * @p assignments are read once, here, to build a self-contained request that
 * expires rather than dangles if the runtime is destroyed mid-load; that
 * same expiry gates @p completed, which is simply never called if the
 * runtime owning @p assignments is gone first. See
 * docs/architecture/engine-device-factory.md.
 */
ExternalPluginResolution createEngineExternalDeviceAsync(
    const magda::DeviceInfo& device, magda::engine::DeviceKey key,
    const ExternalPluginServices& services, bool offlineRender,
    const PluginAssignments& assignments, CurrentDeviceLookup currentDevice,
    std::function<void(ExternalDeviceResult)> completed);

/**
 * @brief Whether the scan knows the plugin @p device names.
 *
 * True doesn't promise the plugin will load, only that there is one to try.
 */
bool isInstalledExternalPlugin(const magda::DeviceInfo& device,
                               const juce::KnownPluginList& knownPlugins);

}  // namespace magda::daw::audio::engine_adapter
