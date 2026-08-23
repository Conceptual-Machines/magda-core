#pragma once

#include <memory>

#include "core/DeviceInfo.hpp"
#include "exec/EngineDevice.hpp"

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

}  // namespace magda::daw::audio::engine_adapter
