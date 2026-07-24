#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <memory>
#include <span>
#include <vector>

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
    bool showInBrowser = false;         // listed in the plugin browser (single source of truth)
    bool isInstrument = false;          // browser hint: synth/sampler vs effect
    const char* const* tags = nullptr;  // extensible behavioural classifications
    int tagCount = 0;
    int defaultModulationParamIndex = -1;
    te::Plugin::Ptr (*createInEdit)(const InternalPluginSpec&, te::Edit&,
                                    const juce::String& savedPluginState) = nullptr;
    te::Plugin::Ptr (*createCustomPlugin)(const te::PluginCreationInfo&) = nullptr;
};

struct InternalParameterAliasSpec {
    const char* pluginKey = nullptr;
    const char* alias = nullptr;
    int paramIndex = -1;
    const char* paramName = nullptr;
};

/**
 * Runtime-owned catalog of native device specifications.
 *
 * Device packs populate this catalog through `registerPlugin`; the registry
 * itself deliberately knows nothing about concrete device classes.  This is
 * the seam used by an out-of-tree device pack as well as MAGDA's base pack.
 */
class InternalPluginRegistry {
  public:
    /// Adds a device spec. Duplicate canonical ids and aliases are rejected.
    bool registerPlugin(InternalPluginSpec spec);
    bool registerParameterAlias(InternalParameterAliasSpec alias);

    std::span<const InternalPluginSpec* const> getAll() const;
    std::span<const InternalParameterAliasSpec> getParameterAliases() const;
    const InternalPluginSpec* find(const juce::String& pluginId) const;
    const InternalPluginSpec* findForLoadType(const juce::String& type) const;

  private:
    std::vector<std::unique_ptr<InternalPluginSpec>> specs_;
    std::vector<const InternalPluginSpec*> pointers_;
    std::vector<InternalParameterAliasSpec> parameterAliases_;
};

/// Implemented by a device pack to contribute its concrete device specs.
using DevicePackRegistration = void (*)(InternalPluginRegistry&);

/// Process-wide registry, populated once by the linked device packs.
InternalPluginRegistry& getInternalPluginRegistry();

/// Registers a pack before the catalog is first queried. Returns false once
/// initialization has begun or when the same hook is already registered.
bool registerDevicePack(DevicePackRegistration registerDevices);

std::span<const InternalPluginSpec* const> getAllInternalPluginSpecs();
std::span<const InternalParameterAliasSpec> getAllInternalParameterAliases();

const InternalPluginSpec* findInternalPluginSpec(const juce::String& pluginId);
const InternalPluginSpec* findInternalPluginSpecForLoadType(const juce::String& type);

/// Behavioural tags are pack-defined string identifiers (for example,
/// "analysis" or "midi-generator"). They deliberately avoid recreating a
/// closed core enum as the device catalog grows.
bool internalPluginHasTag(const InternalPluginSpec& spec, const char* tag);
bool internalPluginHasTag(const juce::String& pluginId, const char* tag);
const InternalPluginSpec* findInternalPluginSpecWithTag(const char* tag);
bool isInternalAnalysisPlugin(const juce::String& pluginId);
bool isInternalMidiGeneratorPlugin(const juce::String& pluginId);
int internalPostFxAnalysisOrder(const juce::String& pluginId);

te::Plugin::Ptr createInternalPluginFromSpec(const InternalPluginSpec& spec, te::Edit& edit,
                                             const juce::String& savedPluginState = {});
te::Plugin::Ptr createRegisteredCustomPlugin(const te::PluginCreationInfo& info);

std::unique_ptr<DeviceProcessor> createInternalPluginProcessor(const InternalPluginSpec& spec,
                                                               DeviceId deviceId,
                                                               te::Plugin::Ptr plugin);

}  // namespace magda::daw::audio
