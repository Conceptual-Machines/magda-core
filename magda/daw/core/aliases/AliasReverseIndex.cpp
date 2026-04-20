#include "AliasReverseIndex.hpp"

namespace magda {

std::vector<ReverseMatch> findAliasesByPath(const AliasRegistry& registry,
                                            const ChainNodePath& devicePath, int paramIndex,
                                            bool autoGenOnly) {
    return registry.findByPath(devicePath, paramIndex, autoGenOnly);
}

std::optional<juce::String> bestAliasForPath(const AliasRegistry& registry,
                                             const ChainNodePath& devicePath, int paramIndex,
                                             bool autoGenOnly) {
    auto matches = registry.findByPath(devicePath, paramIndex, autoGenOnly);
    if (matches.empty())
        return std::nullopt;

    // Layer priority order: UserProject(0) < UserGlobal(1) < Curated(2) < AutoGen(3).
    // Pick the match from the highest-priority (lowest numeric) layer.
    const ReverseMatch* best = &matches[0];
    for (const auto& m : matches) {
        if (static_cast<int>(m.layer) < static_cast<int>(best->layer))
            best = &m;
    }
    return best->canonicalName;
}

}  // namespace magda
