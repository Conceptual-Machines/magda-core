#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>

#include "core/DeviceInfo.hpp"
#include "exec/EngineDevice.hpp"
#include "exec/RenderContext.hpp"
#include "plugin_manager/ExternalPluginLookup.hpp"
#include "plugin_manager/ExternalPluginState.hpp"

/**
 * @file EngineDeviceFactory.hpp
 * @brief What a Device op resolves to, asked of the app's own catalogs (#2174).
 *
 * The plan is topology: a Device op names an identity and owns no plugin, and
 * binding one is the host's job the way opening a WAV is. This is that job for
 * the devices MAGDA ships, and it asks the same two catalogs the current engine
 * asks -- the internal registry and the compiled-Faust catalog -- rather than
 * keeping a third list of what exists.
 *
 * It answers for a device that has moved to the SDK (InternalPluginSpec and
 * CompiledPluginSpec both carry `createDevice`) and returns null for one that
 * has not. That null is the honest answer and it is meant to be visible: a
 * device the engine cannot run has to be reported by whoever asked, never
 * quietly replaced by something that passes signal, because a stand-in the
 * incumbent does not have is a divergence wearing the costume of a null.
 *
 * Parameters are not written here. The plan's value layer resolves every one of
 * them per block and the adapter writes them before each process() call, so a
 * device created here starts at its own defaults and is at the project's values
 * by the first sample. What is not carried yet is the rest of a device's state
 * -- what MagdaDevice::restoreState() takes -- which is the pluginState v2
 * contract (#1887) and belongs with the state slice rather than with this one.
 *
 * The second half of the file is the same job for a plugin MAGDA did not write
 * (#2243). It is a different job in one respect that shapes everything below:
 * an internal device is a class this build contains and either exists or does
 * not, while an external plugin is a file on a machine that may have moved,
 * been upgraded, or never been installed. So creating one can fail for reasons
 * worth telling a user about, and it returns why rather than a null.
 *
 * An external plugin's saved state does travel, unlike an internal device's:
 * MAGDA persists the plugin's own chunk, and the adapter applies it along with
 * the saved parameter array it overlays (EngineExternalDevice::applySavedState).
 * What that leaves for the state slice (#2244) is the other direction --
 * writing the chunk back out of an instance the native engine holds, so a
 * project round-trips between the two engines during the dual-engine release.
 */

namespace magda::daw::audio::engine_adapter {

/**
 * @brief The engine device @p device names, or null when nothing can make one.
 *
 * Null for a device whose id is in neither catalog, and for one that is in a
 * catalog but has not moved to the SDK yet.
 *
 * @p offlineRender says which kind of render this instance is being built for,
 * and defaults to a live one. See EngineMagdaDevice's constructor: it decides
 * what a device is told about isRendering, and the default is the reading a
 * device may not get wrong.
 */
std::unique_ptr<magda::engine::EngineDevice> createEngineDevice(const magda::DeviceInfo& device,
                                                                bool offlineRender = false);

/**
 * @brief Whether @p pluginId names a device the engine can run.
 *
 * The same question createEngineDevice() answers, without building one. What
 * asks is anything that has to report a project's unrunnable devices before it
 * renders: a corpus case, and the bridge at cutover.
 */
bool canCreateEngineDevice(const juce::String& pluginId);

/**
 * @brief Whether either catalog knows @p pluginId at all.
 *
 * A different question from the one above, and the two together are what
 * separates a device the engine is missing from a device there is none of.
 * False for an id nothing registered -- an external plugin, a device written
 * for a test, an empty id on a chain slot that stands for nothing -- and for
 * those the engine running nothing is what every host does.
 *
 * True with canCreateEngineDevice() false is the case worth reporting: the app
 * can build this device and the engine cannot, so a render that passed the
 * signal through is a render of a different project.
 */
bool isRegisteredDevice(const juce::String& pluginId);

/**
 * @brief What a host hands over before the engine can make an external plugin.
 *
 * Two things the engine has no way to own: the formats this build can load, and
 * what the last scan found installed. Both are the app's, and both are plain
 * JUCE rather than anything of the incumbent engine's, which is what lets the
 * native engine ask for them without reaching into it.
 *
 * The render context is here because a plugin is told the rate and block size
 * as it is created, before anything prepares it. It is prepared again by the
 * adapter, so this is not the authority on either number -- it is what a plugin
 * that allocates at construction has to go on.
 */
struct ExternalPluginServices {
    juce::AudioPluginFormatManager* formats = nullptr;
    const juce::KnownPluginList* knownPlugins = nullptr;
    magda::engine::RenderContext context{};
};

/**
 * @brief An external device, or why there is not one.
 *
 * Exactly one of the two is set. The failure is a sentence for a person: which
 * plugin, and what went wrong finding or loading it. A caller that has nowhere
 * to put it may drop it; a caller rendering a corpus case prints it, because a
 * project rendered without its plugin is a render of a different project.
 */
struct ExternalDeviceResult {
    std::unique_ptr<magda::engine::EngineDevice> device;
    juce::String failure;

    /**
     * @brief What the plugin holds now, for the caller to write into its model.
     *
     * Restoring a plugin is not only something done to the plugin: a project's
     * saved parameter array and the chunk beside it can disagree, the chunk
     * wins, and everything downstream reads the model rather than the plugin.
     * An automation lane's base value, a knob's position, and the values a
     * render writes every block all come from there, so a model left holding
     * the stale array would put them back on the next block.
     *
     * The engine does not correct the model itself and could not: the model is
     * the one authority and this is a render path. So the correction is handed
     * back, and applying it is the caller's, on the message thread
     * (magda::applyRestoredParameters). A caller that already knows its model
     * agrees with the plugin -- a project saved by a build that refreshed it --
     * applies the same values it already had.
     *
     * Empty when nothing was created.
     */
    std::vector<magda::RestoredParameter> restoredParameters;
};

/**
 * @brief The adapter over an instance the host made itself.
 *
 * What both entry points below end at, and the seam for a host that creates its
 * plugins some other way. One transaction: the instance's buses are enabled,
 * which has to happen before anything reads its channel counts; the project's
 * saved parameter array and its chunk are applied in that order; and what the
 * plugin holds afterwards comes back in the result for the caller to write into
 * its model (ExternalPluginState.hpp).
 */
ExternalDeviceResult adaptExternalPluginInstance(
    std::unique_ptr<juce::AudioPluginInstance> instance, const magda::DeviceInfo& device,
    bool offlineRender = false);

/**
 * @brief The external plugin @p device names, created and ready to bind.
 *
 * Blocking: it resolves the description, loads the plugin and returns. What
 * that suits is a render waiting for the answer -- an offline bounce, a corpus
 * case -- where there is nothing to do until the plugin is there.
 *
 * A session opening a project wants the asynchronous form below instead, since
 * a plugin can take seconds to load and a project with forty of them would
 * otherwise open in a minute.
 */
ExternalDeviceResult createEngineExternalDevice(const magda::DeviceInfo& device,
                                                const ExternalPluginServices& services,
                                                bool offlineRender = false);

/**
 * @brief What the model says about this device now, asked on the message thread.
 *
 * A plugin takes seconds to load and a project does not stop while it does. By
 * the time one arrives its device may have been edited, had a preset applied,
 * or been deleted, and the restoration is about to write the model's parameter
 * values onto the instance and hand corrected ones back. Doing that from a copy
 * taken when the load was requested would quietly undo whatever happened in
 * between.
 *
 * So the model is read at completion rather than captured at request. Null
 * means the device is gone, and the load is abandoned rather than completed
 * against a project that no longer has it.
 */
using CurrentDeviceLookup = std::function<const magda::DeviceInfo*()>;

/**
 * @brief What an asynchronous load remembers about what it asked for.
 *
 * The identity is the model's, taken when the load was requested, so that
 * comparing it with the model's identity at completion compares like with like.
 * Not the scanned description's identifier: that carries the plugin's numeric
 * uid, a device's saved fields do not, and comparing the two would call every
 * unchanged plugin a change.
 */
struct RequestedPlugin {
    magda::SavedPluginIdentity identity;

    /// For the messages, which name a plugin rather than an identifier.
    juce::String name;
};

/**
 * @brief The end of an asynchronous load, once the instance exists.
 *
 * The seam the callback below ends at, and a host with a loader of its own
 * calls it directly. @p error is what the loader reported, used only when @p
 * instance is null.
 *
 * It resolves @p currentDevice first: a device that has gone, or that now names
 * a different plugin from the one that was asked for, is a load whose answer
 * arrived too late to be useful, and completing it would put a stale patch on a
 * device somebody has since changed.
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
 * failed. Until then the plan has no device bound for that op, which the
 * executor already handles the way the incumbent does: the op passes its audio
 * through and says so in the diagnostics. Completion is therefore a re-prepare
 * rather than a hand-off -- bindings are resolved when a plan is prepared, so a
 * device that arrives later is published and the plan prepared again.
 *
 * @p device is read once, now, to work out which plugin to load.
 * @p currentDevice is read again at completion, to work out what to restore
 * onto it; see CurrentDeviceLookup for why those are two different questions.
 */
void createEngineExternalDeviceAsync(const magda::DeviceInfo& device,
                                     const ExternalPluginServices& services, bool offlineRender,
                                     CurrentDeviceLookup currentDevice,
                                     std::function<void(ExternalDeviceResult)> completed);

/**
 * @brief Whether the scan knows the plugin @p device names.
 *
 * The question a report asks before it renders: a project whose plugin is not
 * installed on this machine renders without it in both engines, and that is
 * worth saying once rather than being read off a residual afterwards. True does
 * not promise the plugin will load -- a scanned plugin can still fail to
 * instantiate -- only that there is one to try.
 */
bool isInstalledExternalPlugin(const magda::DeviceInfo& device,
                               const juce::KnownPluginList& knownPlugins);

}  // namespace magda::daw::audio::engine_adapter
