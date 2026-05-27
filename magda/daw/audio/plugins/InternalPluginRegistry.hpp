#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <memory>
#include <span>

#include "core/InternalDeviceKind.hpp"
#include "core/TypeIds.hpp"

namespace magda {
class DeviceProcessor;
}

namespace magda::daw::audio {

namespace te = tracktion::engine;

enum class InternalPluginCreateMode {
    Unsupported,
    SavedStateOrFresh,
    FreshValueTree,
    LevelMeterValueTree,
};

struct InternalPluginSpec {
    InternalDeviceKind kind = InternalDeviceKind::External;
    const char* pluginId = nullptr;
    const char* displayName = nullptr;
    const char* browserCategory = nullptr;
    const char* description = nullptr;
    InternalPluginCreateMode createMode = InternalPluginCreateMode::Unsupported;
    bool canCreateDetached = true;
    bool canCreateOnTrack = true;
    const char* const* loadAliases = nullptr;
    int loadAliasCount = 0;
    bool (*matchesPlugin)(te::Plugin*) = nullptr;
    std::unique_ptr<DeviceProcessor> (*createProcessor)(DeviceId, te::Plugin::Ptr) = nullptr;
    bool showInBrowser = false;  // listed in the plugin browser (single source of truth)
    bool isInstrument = false;   // browser hint: synth/sampler vs effect
};

std::span<const InternalPluginSpec* const> getAllInternalPluginSpecs();

const InternalPluginSpec* findInternalPluginSpec(InternalDeviceKind kind);
const InternalPluginSpec* findInternalPluginSpec(const juce::String& pluginId);
const InternalPluginSpec* findInternalPluginSpecForLoadType(const juce::String& type);

te::Plugin::Ptr createInternalPluginFromSpec(const InternalPluginSpec& spec, te::Edit& edit,
                                             const juce::String& savedPluginState = {});

std::unique_ptr<DeviceProcessor> createInternalPluginProcessor(const InternalPluginSpec& spec,
                                                               DeviceId deviceId,
                                                               te::Plugin::Ptr plugin);

}  // namespace magda::daw::audio
