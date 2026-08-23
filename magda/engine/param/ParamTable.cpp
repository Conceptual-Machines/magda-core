#include "param/ParamTable.hpp"

#include <algorithm>

namespace magda::engine {

namespace {

/// FNV-1a, as the plan's fingerprint is: it has to separate layouts that
/// differ, and it runs off the audio thread once per compile.
class Fingerprint {
  public:
    void mix(std::uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            hash_ ^= (value >> (byte * 8)) & 0xff;
            hash_ *= 1099511628211ULL;
        }
    }

    void mix(const ParamKey& key) {
        mix(static_cast<std::uint64_t>(key.kind));
        mix(static_cast<std::uint64_t>(key.scope));
        mix(static_cast<std::uint64_t>(key.trackId));
        mix(static_cast<std::uint64_t>(key.rackId));
        mix(static_cast<std::uint64_t>(key.device.segment));
        mix(static_cast<std::uint64_t>(key.device.deviceId));
        mix(static_cast<std::uint64_t>(key.modId));
        mix(static_cast<std::uint64_t>(key.index));
    }

    std::uint64_t value() const {
        return hash_;
    }

  private:
    std::uint64_t hash_ = 14695981039346656037ULL;
};

}  // namespace

std::uint64_t paramLayoutFingerprint(const std::vector<ParamKey>& keys) {
    Fingerprint hash;
    hash.mix(keys.size());

    for (const auto& key : keys)
        hash.mix(key);

    return hash.value();
}

std::uint64_t paramModifierFingerprint(const std::vector<ParamModifier>& modifiers) {
    Fingerprint hash;
    hash.mix(modifiers.size());

    for (const auto& modifier : modifiers) {
        hash.mix(modifier.key);

        // The kind as well as the address, because the model changes one
        // without moving the other: switching a modifier from an LFO to a
        // random is an edit in place, and it would otherwise travel as a
        // values publish that the runtime is never re-prepared for. The engine
        // behind an address is structural even when the address is not.
        hash.mix(static_cast<std::uint64_t>(modifier.kind));
    }

    return hash.value();
}

std::span<const ParamLink> ParamTable::linksFor(ParamId param) const {
    if (param < 0 || param + 1 >= static_cast<ParamId>(linkOffsets.size()))
        return {};

    const auto first = static_cast<std::size_t>(linkOffsets[static_cast<std::size_t>(param)]);
    const auto last = static_cast<std::size_t>(linkOffsets[static_cast<std::size_t>(param) + 1]);
    if (last <= first || last > links.size())
        return {};

    return std::span<const ParamLink>{links}.subspan(first, last - first);
}

std::span<const magda::AutomationPoint> ParamTable::curveFor(ParamId param) const {
    if (param < 0 || param + 1 >= static_cast<ParamId>(curveOffsets.size()))
        return {};

    const auto first = static_cast<std::size_t>(curveOffsets[static_cast<std::size_t>(param)]);
    const auto last = static_cast<std::size_t>(curveOffsets[static_cast<std::size_t>(param) + 1]);
    if (last <= first || last > curvePoints.size())
        return {};

    return std::span<const magda::AutomationPoint>{curvePoints}.subspan(first, last - first);
}

std::span<const magda::CurvePointData> ParamTable::modCurveFor(int modifier) const {
    if (modifier < 0 || modifier + 1 >= static_cast<int>(modCurveOffsets.size()))
        return {};

    const auto first =
        static_cast<std::size_t>(modCurveOffsets[static_cast<std::size_t>(modifier)]);
    const auto last =
        static_cast<std::size_t>(modCurveOffsets[static_cast<std::size_t>(modifier) + 1]);
    if (last <= first || last > modCurvePoints.size())
        return {};

    return std::span<const magda::CurvePointData>{modCurvePoints}.subspan(first, last - first);
}

}  // namespace magda::engine
