#include "ResolverRegistry.hpp"

#include "../TypeIds.hpp"

namespace magda {

// ============================================================================
// ResolverRegistry singleton
// ============================================================================

ResolverRegistry& ResolverRegistry::getInstance() {
    static ResolverRegistry instance;
    return instance;
}

ResolverRegistry::ResolverRegistry() {
    registerResolver(std::make_unique<FocusedDeviceMacroResolver>());
    registerResolver(std::make_unique<SelectedTrackVolumeResolver>());
    registerResolver(std::make_unique<SelectedTrackPanResolver>());
    registerResolver(std::make_unique<MasterVolumeResolver>());
    registerResolver(std::make_unique<MasterPanResolver>());
}

const AliasResolver* ResolverRegistry::findResolver(const juce::String& kind) const {
    for (const auto& resolver : resolvers_) {
        if (resolver->kind() == kind)
            return resolver.get();
    }
    return nullptr;
}

void ResolverRegistry::registerResolver(std::unique_ptr<AliasResolver> resolver) {
    // Replace if kind already registered
    for (auto& existing : resolvers_) {
        if (existing->kind() == resolver->kind()) {
            existing = std::move(resolver);
            return;
        }
    }
    resolvers_.push_back(std::move(resolver));
}

// ============================================================================
// FocusedDeviceMacroResolver
// ============================================================================

std::optional<StaticTarget> FocusedDeviceMacroResolver::resolve(const juce::StringPairArray& args,
                                                                const ChainContext& ctx) const {
    auto devicePath = ctx.focusedDevice();
    if (!devicePath.isValid())
        return std::nullopt;

    int macroIndex = args.getValue("macroIndex", "0").getIntValue();

    StaticTarget t;
    t.devicePath = devicePath;
    t.paramIndex = macroIndex;
    t.owner = StaticTarget::Owner::DeviceMacro;
    DBG("[AUTOMAP] FocusedDeviceMacroResolver: returning StaticTarget macroIndex="
        << macroIndex << " owner=DeviceMacro trackId=" << devicePath.trackId
        << " deviceId=" << devicePath.getDeviceId());
    return t;
}

// ============================================================================
// SelectedTrackVolumeResolver
// ============================================================================

std::optional<StaticTarget> SelectedTrackVolumeResolver::resolve(
    const juce::StringPairArray& /*args*/, const ChainContext& ctx) const {
    TrackId trackId = ctx.selectedTrack();
    if (trackId == INVALID_TRACK_ID)
        return std::nullopt;

    // Track volume is represented as a track-level path with paramIndex 0
    StaticTarget t;
    t.devicePath = ChainNodePath::trackLevel(trackId);
    t.paramIndex = 0;  // volume
    return t;
}

// ============================================================================
// SelectedTrackPanResolver
// ============================================================================

std::optional<StaticTarget> SelectedTrackPanResolver::resolve(const juce::StringPairArray& /*args*/,
                                                              const ChainContext& ctx) const {
    TrackId trackId = ctx.selectedTrack();
    if (trackId == INVALID_TRACK_ID)
        return std::nullopt;

    StaticTarget t;
    t.devicePath = ChainNodePath::trackLevel(trackId);
    t.paramIndex = 1;  // pan
    return t;
}

// ============================================================================
// MasterVolumeResolver
// ============================================================================

std::optional<StaticTarget> MasterVolumeResolver::resolve(const juce::StringPairArray& /*args*/,
                                                          const ChainContext& /*ctx*/) const {
    StaticTarget t;
    t.devicePath = ChainNodePath::trackLevel(MASTER_TRACK_ID);
    t.paramIndex = 0;  // volume
    return t;
}

// ============================================================================
// MasterPanResolver
// ============================================================================

std::optional<StaticTarget> MasterPanResolver::resolve(const juce::StringPairArray& /*args*/,
                                                       const ChainContext& /*ctx*/) const {
    StaticTarget t;
    t.devicePath = ChainNodePath::trackLevel(MASTER_TRACK_ID);
    t.paramIndex = 1;  // pan
    return t;
}

}  // namespace magda
