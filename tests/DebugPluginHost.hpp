// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdlib>
#include <optional>

#include "debug_plugin/DebugPluginFaults.hpp"
#include "magda/daw/core/DeviceInfo.hpp"

/**
 * @file DebugPluginHost.hpp
 * @brief The debug plugin (tests/debug_plugin) as the hosting tests load it (#3155).
 */

namespace magda::test {

/// The built bundle, scanned straight from the build tree rather than the system folders.
inline std::optional<juce::PluginDescription> debugPluginDescription() {
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile(found, MAGDA_DEBUG_PLUGIN_PATH);
    if (found.isEmpty())
        return std::nullopt;
    return *found.getFirst();
}

/// The device the plugin browser would add for @p description.
inline DeviceInfo debugPluginDevice(const juce::PluginDescription& description, DeviceId id) {
    DeviceInfo device;
    device.id = id;
    device.name = description.name;
    device.manufacturer = description.manufacturerName;
    device.pluginId = description.createIdentifierString();
    device.uniqueId = description.createIdentifierString();
    device.fileOrIdentifier = description.fileOrIdentifier;
    device.format = PluginFormat::VST3;
    device.isInstrument = description.isInstrument;
    device.deviceType = DeviceType::Instrument;
    return device;
}

/// The faults every instance created in this scope reads; cleared on the way out.
class ScopedDebugPluginFaults {
  public:
    explicit ScopedDebugPluginFaults(const juce::String& spec) {
        set(spec);
    }

    ~ScopedDebugPluginFaults() {
        set({});
    }

    ScopedDebugPluginFaults(const ScopedDebugPluginFaults&) = delete;
    ScopedDebugPluginFaults& operator=(const ScopedDebugPluginFaults&) = delete;

  private:
    static void set(const juce::String& spec) {
#if JUCE_WINDOWS
        // Writes the process environment too, which is what the plugin's JUCE reads.
        _putenv_s(debug_plugin::kFaultsVariable, spec.toRawUTF8());
#else
        if (spec.isEmpty())
            unsetenv(debug_plugin::kFaultsVariable);
        else
            setenv(debug_plugin::kFaultsVariable, spec.toRawUTF8(), 1);
#endif
    }
};

}  // namespace magda::test
