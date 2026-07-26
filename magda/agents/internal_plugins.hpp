#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "../daw/audio/plugins/InternalPluginRegistry.hpp"
#include "../daw/audio/plugins/compiled/CompiledPluginRegistry.hpp"
#include "../daw/core/DeviceInfo.hpp"
#include "../daw/core/PluginAlias.hpp"
#include "../daw/core/aliases/InternalPluginAliases.hpp"

namespace magda {

/**
 * @brief Device-specific AI handlers that predate the registry-derived agent
 * catalog. Only the values used to route those handlers are retained here;
 * every addable device is represented by InternalPluginInfo below.
 */
enum class InternalPlugin {
    None,
    FourOsc,
    Faust,
    StepSequencer,
    PolyStepSequencer,
};

enum class InternalPluginVendor {
    TracktionEngine,
    Magda,
};

enum class SoundDesignAgentKind {
    None,
    FourOsc,
    StepSequencer,
    PolyStepSequencer,
    // Generic parameter-introspection agent — any sound-generator instrument
    // whose controls are automatable parameters (compiled Faust synths, native
    // Mutable ports). See detail::isGenericSoundGeneratorId.
    Generic,
};

enum class CoderAgentKind {
    None,
    Faust,
};

/**
 * Declarative agent-facing capabilities for an internal device.
 *
 * UI visibility, specialised-agent routing, and command context all read this
 * same structure. An absent catalog entry therefore has an unambiguous empty
 * capability state instead of falling through a collection of feature-specific
 * plugin-id switches.
 */
struct InternalPluginCapabilities {
    bool addable = false;
    bool automatable = false;
    bool drumRoleProvider = false;
    SoundDesignAgentKind soundDesignAgent = SoundDesignAgentKind::None;
    CoderAgentKind coderAgent = CoderAgentKind::None;
    std::vector<juce::String> parameterAliases;

    bool supportsSoundDesign() const {
        return soundDesignAgent != SoundDesignAgentKind::None;
    }
    bool supportsCoder() const {
        return coderAgent != CoderAgentKind::None;
    }
    bool supportsDeviceAI() const {
        return supportsSoundDesign() || supportsCoder();
    }
};

/**
 * @brief Agent-facing view of one internal device.
 *
 * This is deliberately derived from the audio registries rather than being a
 * second hand-maintained plugin list.  `aliases` contains compatibility names
 * accepted by the command resolver; `primaryAlias` is the one shown in agent
 * autocomplete.
 */
struct InternalPluginInfo {
    juce::String displayName;
    juce::String pluginId;
    DeviceType deviceType = DeviceType::Effect;
    InternalPlugin id = InternalPlugin::None;
    InternalPluginVendor vendor = InternalPluginVendor::Magda;
    juce::String primaryAlias;
    std::vector<juce::String> aliases;
    bool browserVisible = false;
    InternalPluginCapabilities capabilities;
};

namespace detail {

inline void addAlias(InternalPluginInfo& entry, const juce::String& alias) {
    const auto trimmed = alias.trim();
    if (trimmed.isEmpty())
        return;

    for (const auto& existing : entry.aliases) {
        if (existing.equalsIgnoreCase(trimmed))
            return;
    }
    entry.aliases.push_back(trimmed);
}

inline InternalPluginVendor vendorFor(const daw::audio::InternalPluginSpec& spec) {
    return daw::audio::internalPluginHasTag(spec, "tracktion-engine")
               ? InternalPluginVendor::TracktionEngine
               : InternalPluginVendor::Magda;
}

inline InternalPlugin deviceAiIdFor(const juce::String& pluginId) {
    if (pluginId.equalsIgnoreCase("4osc"))
        return InternalPlugin::FourOsc;
    if (pluginId.equalsIgnoreCase("faust"))
        return InternalPlugin::Faust;
    if (pluginId.equalsIgnoreCase("stepsequencer"))
        return InternalPlugin::StepSequencer;
    if (pluginId.equalsIgnoreCase("polystepsequencer"))
        return InternalPlugin::PolyStepSequencer;
    return InternalPlugin::None;
}

// True iff `pluginId` is a sound-generator instrument the generic
// parameter-introspection agent can drive: the compiled-Faust synths (Poly
// Synth, FM, percussion) and the native Mutable ports (Elements, Rings).
// Excludes Sampler / Drum Grid / Clouds (AI adds little / not a note source)
// and 4OSC, which keeps its bespoke agent until its controls are automatable.
inline bool isGenericSoundGeneratorId(const juce::String& pluginId) {
    if (pluginId.isEmpty())
        return false;
    if (const auto* spec = daw::audio::compiled::findCompiledPluginSpec(pluginId))
        return spec->isInstrument;
    return daw::audio::internalPluginHasTag(pluginId, "mutable-instrument");
}

inline InternalPluginCapabilities capabilitiesFor(const juce::String& pluginId) {
    InternalPluginCapabilities capabilities;
    capabilities.addable = true;
    capabilities.automatable = !pluginId.equalsIgnoreCase("insert");
    capabilities.drumRoleProvider = pluginId.equalsIgnoreCase("drumgrid");

    if (pluginId.equalsIgnoreCase("4osc"))
        capabilities.soundDesignAgent = SoundDesignAgentKind::FourOsc;
    else if (pluginId.equalsIgnoreCase("stepsequencer"))
        capabilities.soundDesignAgent = SoundDesignAgentKind::StepSequencer;
    else if (pluginId.equalsIgnoreCase("polystepsequencer"))
        capabilities.soundDesignAgent = SoundDesignAgentKind::PolyStepSequencer;
    else if (pluginId.equalsIgnoreCase("faust") || pluginId.equalsIgnoreCase("faustinstrument"))
        capabilities.coderAgent = CoderAgentKind::Faust;
    else if (isGenericSoundGeneratorId(pluginId))
        capabilities.soundDesignAgent = SoundDesignAgentKind::Generic;

    return capabilities;
}

inline void addParameterAlias(InternalPluginInfo& entry, const juce::String& alias) {
    const auto trimmed = alias.trim();
    if (trimmed.isEmpty())
        return;
    for (const auto& existing : entry.capabilities.parameterAliases) {
        if (existing.equalsIgnoreCase(trimmed))
            return;
    }
    entry.capabilities.parameterAliases.push_back(trimmed);
}

inline InternalPluginInfo makeEntry(const juce::String& displayName, const juce::String& pluginId,
                                    DeviceType deviceType, InternalPluginVendor vendor,
                                    bool browserVisible) {
    InternalPluginInfo entry;
    entry.displayName = displayName;
    entry.pluginId = pluginId;
    entry.deviceType = deviceType;
    entry.id = deviceAiIdFor(pluginId);
    entry.vendor = vendor;
    entry.primaryAlias = pluginNameToAlias(displayName);
    entry.browserVisible = browserVisible;
    entry.capabilities = capabilitiesFor(pluginId);

    addAlias(entry, entry.primaryAlias);
    addAlias(entry, displayName);
    addAlias(entry, pluginId);
    return entry;
}

inline void addExternalInsertVariants(std::vector<InternalPluginInfo>& entries) {
    const auto* spec = daw::audio::findInternalPluginSpecWithTag("external-insert");
    if (spec == nullptr || spec->pluginId == nullptr)
        return;

    auto fx = makeEntry("External FX", spec->pluginId, DeviceType::Effect,
                        InternalPluginVendor::TracktionEngine, true);
    addAlias(fx, "external insert");
    entries.push_back(std::move(fx));

    auto instrument = makeEntry("External Instrument", spec->pluginId, DeviceType::Instrument,
                                InternalPluginVendor::TracktionEngine, true);
    entries.push_back(std::move(instrument));
}

inline void makePrimaryAliasesUnique(std::vector<InternalPluginInfo>& entries) {
    for (size_t i = 0; i < entries.size(); ++i) {
        auto& entry = entries[i];
        const auto isTaken = [&](const juce::String& alias) {
            for (size_t previous = 0; previous < i; ++previous) {
                if (entries[previous].primaryAlias.equalsIgnoreCase(alias))
                    return true;
            }
            return false;
        };

        if (!isTaken(entry.primaryAlias))
            continue;

        const auto prefix = entry.vendor == InternalPluginVendor::TracktionEngine
                                ? juce::String("tracktion_")
                                : juce::String("magda_");
        const auto base = prefix + entry.primaryAlias;
        auto uniqueAlias = base;
        for (int suffix = 2; isTaken(uniqueAlias); ++suffix)
            uniqueAlias = base + "_" + juce::String(suffix);

        entry.primaryAlias = uniqueAlias;
        addAlias(entry, uniqueAlias);
    }
}

}  // namespace detail

/**
 * @brief All internal devices that the agent may add to a track.
 *
 * Native specs with Unsupported creation are deliberately excluded: they are
 * transport, metering, or session helper devices and cannot be user-created.
 * Browser-visible External Insert is represented by separate FX and instrument
 * entries to retain the browser's placement semantics.
 */
inline const std::vector<InternalPluginInfo>& getInternalPlugins() {
    static const std::vector<InternalPluginInfo> kPlugins = [] {
        std::vector<InternalPluginInfo> entries;

        // The browser-visible compiled devices are preferred for duplicate
        // display names (EQ, Reverb, Delay, ...). Legacy TE variants remain
        // addressable below through generated tracktion_* canonical aliases.
        for (const auto* spec : daw::audio::compiled::getAllCompiledPluginSpecs()) {
            if (spec == nullptr || spec->pluginId == nullptr || spec->displayName == nullptr)
                continue;

            auto entry =
                detail::makeEntry(spec->displayName, spec->pluginId,
                                  spec->isInstrument ? DeviceType::Instrument : DeviceType::Effect,
                                  InternalPluginVendor::Magda, true);
            if (spec->aliasKey != nullptr)
                detail::addAlias(entry, spec->aliasKey);
            const auto parameterAliasKey = spec->aliasKey != nullptr ? juce::String(spec->aliasKey)
                                                                     : juce::String(spec->pluginId);
            for (int i = 0; i < spec->aliasCount; ++i) {
                if (spec->aliases != nullptr && spec->aliases[i].alias != nullptr)
                    detail::addParameterAlias(entry,
                                              parameterAliasKey + "." + spec->aliases[i].alias);
            }
            entries.push_back(std::move(entry));
        }

        for (const auto* spec : daw::audio::getAllInternalPluginSpecs()) {
            if (spec == nullptr || spec->pluginId == nullptr || spec->displayName == nullptr ||
                spec->createMode == daw::audio::InternalPluginCreateMode::Unsupported ||
                !spec->canCreateOnTrack ||
                daw::audio::internalPluginHasTag(*spec, "external-insert"))
                continue;

            auto entry =
                detail::makeEntry(spec->displayName, spec->pluginId,
                                  spec->isInstrument ? DeviceType::Instrument : DeviceType::Effect,
                                  detail::vendorFor(*spec), spec->showInBrowser);
            for (int i = 0; i < spec->loadAliasCount; ++i) {
                if (spec->loadAliases[i] != nullptr)
                    detail::addAlias(entry, spec->loadAliases[i]);
            }
            entries.push_back(std::move(entry));
        }

        detail::addExternalInsertVariants(entries);

        // The code-driven curated layer is the source of truth for legacy
        // Tracktion-device parameter aliases. Attach its canonical names to
        // matching catalog entries so agents can discover the same vocabulary
        // accepted by automation commands.
        for (const auto& [canonicalName, alias] : collectInternalPluginCuratedAliases()) {
            for (auto& entry : entries) {
                bool matches =
                    entry.primaryAlias.equalsIgnoreCase(alias.pluginTypeKey) ||
                    entry.pluginId.equalsIgnoreCase(alias.pluginTypeKey) ||
                    pluginNameToAlias(entry.displayName).equalsIgnoreCase(alias.pluginTypeKey);
                if (!matches) {
                    for (const auto& deviceAlias : entry.aliases) {
                        if (deviceAlias.equalsIgnoreCase(alias.pluginTypeKey)) {
                            matches = true;
                            break;
                        }
                    }
                }
                if (matches)
                    detail::addParameterAlias(entry, canonicalName);
            }
        }
        detail::makePrimaryAliasesUnique(entries);
        return entries;
    }();
    return kPlugins;
}

/// True iff `pluginId` is a stock Tracktion Engine plugin.
inline bool isTracktionEngineStockPlugin(const juce::String& pluginId) {
    for (const auto& entry : getInternalPlugins()) {
        if (entry.pluginId.equalsIgnoreCase(pluginId))
            return entry.vendor == InternalPluginVendor::TracktionEngine;
    }
    return false;
}

/** Resolve a plugin id for device-specific AI routing. */
inline InternalPlugin internalPluginFromId(const juce::String& pluginId) {
    return detail::deviceAiIdFor(pluginId);
}

inline const InternalPluginInfo* lookupInternalPluginById(const juce::String& pluginId) {
    for (const auto& entry : getInternalPlugins()) {
        if (entry.pluginId.equalsIgnoreCase(pluginId))
            return &entry;
    }
    return nullptr;
}

inline const InternalPluginCapabilities& getInternalPluginCapabilities(
    const juce::String& pluginId) {
    if (const auto* entry = lookupInternalPluginById(pluginId))
        return entry->capabilities;
    static const InternalPluginCapabilities kUnsupported;
    return kUnsupported;
}

/**
 * @brief Resolve an agent-facing name to an internal device.
 *
 * Exact display names, canonical aliases, registry load aliases and internal
 * ids are all accepted. The normalized comparison makes natural forms such as
 * "pitch shift" resolve to the same entry as their autocomplete alias.
 */
inline const InternalPluginInfo* lookupInternalPluginByAlias(const juce::String& alias) {
    const auto raw = alias.trim();
    if (raw.isEmpty())
        return nullptr;
    const auto normalized = pluginNameToAlias(raw);

    for (const auto& entry : getInternalPlugins()) {
        if (entry.displayName.equalsIgnoreCase(raw) || entry.pluginId.equalsIgnoreCase(raw))
            return &entry;

        for (const auto& candidate : entry.aliases) {
            if (candidate.equalsIgnoreCase(raw) || candidate.equalsIgnoreCase(normalized) ||
                pluginNameToAlias(candidate).equalsIgnoreCase(normalized))
                return &entry;
        }
    }
    return nullptr;
}

/**
 * Build device documentation from the same catalog used by execution.
 *
 * Each listed canonical alias is accepted by lookupInternalPluginByAlias(), so
 * the command prompt never advertises an internal-device token it cannot run.
 */
inline juce::String getInternalPluginCatalogDescription() {
    juce::StringArray effects;
    juce::StringArray instruments;
    juce::StringArray listedDisplayNames;
    for (const auto& entry : getInternalPlugins()) {
        if (!entry.browserVisible)
            continue;

        // A display name may have a hidden legacy counterpart. The catalog is
        // ordered so the browser-visible compiled version supplies the alias
        // shown to the command agent.
        if (listedDisplayNames.contains(entry.displayName, true))
            continue;
        listedDisplayNames.add(entry.displayName);

        auto& names = entry.deviceType == DeviceType::Instrument ? instruments : effects;
        names.add(entry.displayName + " [" + entry.primaryAlias + "]");
    }
    effects.sort(true);
    instruments.sort(true);

    juce::String out("INTERNAL DEVICE CATALOG (use an exact display name or a canonical alias "
                     "in brackets with fx.add; every listed alias is accepted):\n");
    out << "Effects: " << effects.joinIntoString(", ") << "\n";
    out << "Instruments: " << instruments.joinIntoString(", ");
    return out;
}

}  // namespace magda
