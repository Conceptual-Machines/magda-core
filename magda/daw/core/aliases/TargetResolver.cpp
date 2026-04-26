#include "TargetResolver.hpp"

#include "../PluginAlias.hpp"
#include "ParamNameNormalize.hpp"

namespace magda {

// ============================================================================
// resolve(Target)
// ============================================================================

ResolvedTarget TargetResolver::resolve(const Target& target) const {
    return std::visit(
        [this](const auto& t) -> ResolvedTarget {
            using T = std::decay_t<decltype(t)>;

            if constexpr (std::is_same_v<T, StaticTarget>) {
                if (!t.isValid())
                    return ResolvedTarget::failure("StaticTarget is invalid");

                ResolvedTarget r;
                r.devicePath = t.devicePath;
                r.paramIndex = t.paramIndex;
                r.owner = t.owner;
                r.modId = t.modId;
                r.modParamIndex = t.modParamIndex;
                r.sourceLabel = "static";
                r.resolved = true;
                return r;

            } else if constexpr (std::is_same_v<T, AliasRef>) {
                auto stored = aliasRegistry_.lookupStored(t.name, t.pluginType);
                if (!stored.has_value())
                    return ResolvedTarget::failure("Alias not found: " + t.name);

                if (stored->path.has_value()) {
                    ResolvedTarget r;
                    r.devicePath = *stored->path;
                    r.paramIndex = stored->paramIndex;
                    r.sourceLabel = "@" + t.name;
                    r.resolved = true;
                    return r;
                }

                // Path absent -- try to materialise against focused chain
                auto devices = chainContext_.devicesInFocusedChain();
                for (const auto& dw : devices) {
                    if (dw.device == nullptr)
                        continue;
                    auto alias = pluginNameToAlias(dw.device->name);
                    if (alias == stored->pluginTypeKey) {
                        int paramIdx = findParamByKey(*dw.device, normalizeParamName(t.name));
                        if (paramIdx < 0)
                            paramIdx = stored->paramIndex;  // fallback to stored index

                        ResolvedTarget r;
                        r.devicePath = dw.path;
                        r.paramIndex = paramIdx;
                        r.sourceLabel = "@" + t.name + " (materialised)";
                        r.resolved = true;
                        return r;
                    }
                }

                return ResolvedTarget::failure("Alias path absent and no matching device in "
                                               "focused chain: " +
                                               t.name);

            } else if constexpr (std::is_same_v<T, ResolverRef>) {
                const auto* resolver = resolverRegistry_.findResolver(t.kind);
                if (resolver == nullptr)
                    return ResolvedTarget::failure("Unknown resolver kind: " + t.kind);

                auto result = resolver->resolve(t.args, chainContext_);
                if (!result.has_value())
                    return ResolvedTarget::failure("Resolver failed: " + t.kind);

                ResolvedTarget r;
                r.devicePath = result->devicePath;
                r.paramIndex = result->paramIndex;
                r.owner = result->owner;
                r.sourceLabel = "resolver:" + t.kind;
                r.resolved = true;
                DBG("[AUTOMAP] TargetResolver(ResolverRef): kind="
                    << t.kind << " -> paramIndex=" << r.paramIndex << " owner="
                    << (r.owner == StaticTarget::Owner::DeviceMacro ? "DeviceMacro"
                                                                    : "PluginParam"));
                return r;
            }
        },
        target);
}

// ============================================================================
// resolveSigil
// ============================================================================

ResolvedTarget TargetResolver::resolveSigil(const ParsedSigil& sigil) const {
    return resolveAt(sigil);
}

// ============================================================================
// @ sigil
// ============================================================================

ResolvedTarget TargetResolver::resolveAt(const ParsedSigil& sigil) const {
    // Handle scoped forms: @focused.*, @selected.*, @master.*
    if (sigil.isScoped) {
        if (sigil.pluginKey == "master") {
            // @master.volume / @master.pan -- delegate to named resolvers
            juce::String resolverKind = "master." + sigil.paramKey;
            const auto* resolver = resolverRegistry_.findResolver(resolverKind);
            if (resolver != nullptr) {
                juce::StringPairArray args;
                auto result = resolver->resolve(args, chainContext_);
                if (result.has_value()) {
                    ResolvedTarget r;
                    r.devicePath = result->devicePath;
                    r.paramIndex = result->paramIndex;
                    r.sourceLabel = "@" + sigil.pluginKey + "." + sigil.paramKey;
                    r.resolved = true;
                    return r;
                }
            }
            return ResolvedTarget::failure("@master." + sigil.paramKey + " not supported");
        }

        if (sigil.pluginKey == "selected") {
            juce::String resolverKind = "selected." + sigil.paramKey;
            const auto* resolver = resolverRegistry_.findResolver(resolverKind);
            if (resolver != nullptr) {
                juce::StringPairArray args;
                auto result = resolver->resolve(args, chainContext_);
                if (result.has_value()) {
                    ResolvedTarget r;
                    r.devicePath = result->devicePath;
                    r.paramIndex = result->paramIndex;
                    r.sourceLabel = "@" + sigil.pluginKey + "." + sigil.paramKey;
                    r.resolved = true;
                    return r;
                }
            }
            return ResolvedTarget::failure("@selected." + sigil.paramKey +
                                           " not supported or no track selected");
        }

        if (sigil.pluginKey == "focused") {
            // @focused.macro_N -> FocusedDeviceMacroResolver
            auto devicePath = chainContext_.focusedDevice();
            if (!devicePath.isValid())
                return ResolvedTarget::failure("@focused.* -- no device focused");

            const auto* device = chainContext_.deviceAt(devicePath);
            if (device == nullptr)
                return ResolvedTarget::failure("@focused.* -- device not found at path");

            int paramIdx = findParamByKey(*device, normalizeParamName(sigil.paramKey));
            if (paramIdx < 0)
                return ResolvedTarget::failure("@focused." + sigil.paramKey +
                                               " -- param not found on focused device");

            ResolvedTarget r;
            r.devicePath = devicePath;
            r.paramIndex = paramIdx;
            r.sourceLabel = "@focused." + sigil.paramKey;
            r.resolved = true;
            return r;
        }

        return ResolvedTarget::failure("Unknown scope: " + sigil.pluginKey);
    }

    // Non-scoped @name.param
    // Step 1: If a track is selected, scan its chain devices first.
    auto selTrack = chainContext_.selectedTrack();
    if (selTrack != INVALID_TRACK_ID) {
        auto devices = chainContext_.devicesForTrack(selTrack);
        const auto* match = findFirstMatchingDevice(devices, sigil.pluginKey);
        if (match != nullptr && match->device != nullptr) {
            int paramIdx = findParamByKey(*match->device, normalizeParamName(sigil.paramKey));
            if (paramIdx >= 0) {
                ResolvedTarget r;
                r.devicePath = match->path;
                r.paramIndex = paramIdx;
                r.sourceLabel = "@" + sigil.pluginKey + "." + sigil.paramKey + " (selected chain)";
                r.resolved = true;
                return r;
            }
        }
    }

    // Step 2: Fall through to AliasRegistry.
    auto stored =
        aliasRegistry_.lookupStored(sigil.pluginKey + "." + sigil.paramKey, sigil.pluginKey);
    if (!stored.has_value()) {
        // Try just the paramKey as alias name (common convention)
        stored = aliasRegistry_.lookupStored(sigil.paramKey, sigil.pluginKey);
    }

    if (!stored.has_value())
        return ResolvedTarget::failure("Alias not found: @" + sigil.pluginKey + "." +
                                       sigil.paramKey);

    if (stored->path.has_value()) {
        ResolvedTarget r;
        r.devicePath = *stored->path;
        r.paramIndex = stored->paramIndex;
        r.sourceLabel = "@" + sigil.pluginKey + "." + sigil.paramKey;
        r.resolved = true;
        return r;
    }

    // Path absent -> scan focused chain for matching plugin type
    auto devices = chainContext_.devicesInFocusedChain();
    for (const auto& dw : devices) {
        if (dw.device == nullptr)
            continue;
        auto alias = pluginNameToAlias(dw.device->name);
        if (alias == stored->pluginTypeKey) {
            int paramIdx = findParamByKey(*dw.device, normalizeParamName(sigil.paramKey));
            if (paramIdx < 0)
                paramIdx = stored->paramIndex;

            ResolvedTarget r;
            r.devicePath = dw.path;
            r.paramIndex = paramIdx;
            r.sourceLabel = "@" + sigil.pluginKey + "." + sigil.paramKey + " (chain scan)";
            r.resolved = true;
            return r;
        }
    }

    return ResolvedTarget::failure("@" + sigil.pluginKey + "." + sigil.paramKey +
                                   ": alias has no path and no matching device in focused chain");
}

// ============================================================================
// findFirstMatchingDevice
// ============================================================================

// static
const ChainContext::DeviceWithPath* TargetResolver::findFirstMatchingDevice(
    const std::vector<ChainContext::DeviceWithPath>& devices, const juce::String& pluginKey) {
    for (const auto& dw : devices) {
        if (dw.device == nullptr)
            continue;

        // Match against normalised plugin alias OR normalised device name
        auto alias = pluginNameToAlias(dw.device->name);
        bool nameMatch = (alias == pluginKey) || (normalizeParamName(dw.device->name) == pluginKey);

        if (nameMatch)
            return &dw;
    }
    return nullptr;
}

// ============================================================================
// findParamByKey
// ============================================================================

// static
int TargetResolver::findParamByKey(const DeviceInfo& device, const juce::String& paramKey) {
    // First: exact normalised match
    for (const auto& p : device.parameters) {
        if (normalizeParamName(p.name) == paramKey)
            return p.paramIndex;
    }
    // Second: prefix match (paramKey is a prefix of the normalised param name)
    for (const auto& p : device.parameters) {
        if (normalizeParamName(p.name).startsWith(paramKey))
            return p.paramIndex;
    }
    return -1;
}

}  // namespace magda
