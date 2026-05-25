#pragma once

#include <vector>

#include "DeviceInfo.hpp"
#include "RackInfo.hpp"  // ChainElement, deepCopyElement

namespace magda {

/**
 * @brief A single element in a track's post-fader FX chain.
 *
 * Post-FX is flat by design: a linear list of effect / analysis devices that
 * run after the track fader (VolumeAndPan). It is never an instrument (nothing
 * generates sound after the fader) and never a rack (no parallel routing in the
 * post-fader stage). Making it a distinct type from ChainElement encodes those
 * invariants structurally - there is no "is post-fx" flag and no runtime
 * placement check; the type system simply cannot represent a rack or a nested
 * structure here.
 */
struct PostFxChainElement {
    DeviceInfo device;
};

/**
 * @brief The full signal chain of a track, split at the fader.
 *
 * - fxChainElements:     the main / pre-fader FX chain. A full device/rack
 *                        tree (ChainElement), reusing all existing nesting,
 *                        deep-copy, sync-flatten and path-resolution machinery.
 * - postFxChainElements: the post-fader FX chain. A flat list of devices.
 *
 * The track fader (VolumeAndPan) is not modelled as a node; it sits
 * structurally between the two lists. PluginManagerSync realises this as:
 *   flatten(fxChainElements) -> VolumeAndPan -> postFxChainElements -> LevelMeter
 */
struct TrackChain {
    std::vector<ChainElement> fxChainElements;            // main / pre-fader (tree)
    std::vector<PostFxChainElement> postFxChainElements;  // post-fader (flat)

    TrackChain() = default;

    // Move is trivial (default).
    TrackChain(TrackChain&&) = default;
    TrackChain& operator=(TrackChain&&) = default;

    // Copy must deep-copy the pre-fader tree (ChainElement holds
    // unique_ptr<RackInfo>); the post-fader list is plain copyable DeviceInfo.
    TrackChain(const TrackChain& other) {
        copyFrom(other);
    }
    TrackChain& operator=(const TrackChain& other) {
        if (this != &other)
            copyFrom(other);
        return *this;
    }

  private:
    void copyFrom(const TrackChain& other) {
        fxChainElements.clear();
        fxChainElements.reserve(other.fxChainElements.size());
        for (const auto& element : other.fxChainElements)
            fxChainElements.push_back(deepCopyElement(element));
        postFxChainElements = other.postFxChainElements;  // DeviceInfo is copyable
    }
};

}  // namespace magda
