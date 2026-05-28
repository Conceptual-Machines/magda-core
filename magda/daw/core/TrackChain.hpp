#pragma once

#include <vector>

#include "DeviceInfo.hpp"
#include "RackInfo.hpp"  // ChainElement, deepCopyElement

namespace magda {

/**
 * @brief A single element in a track's post-FX chain.
 *
 * Post-FX is flat by design: a linear list of effect / analysis devices that
 * run after the main FX chain (but before the track fader - it is post-FX, not
 * post-fader). It is never an instrument (nothing generates sound at this
 * stage) and never a rack (no parallel routing in the post-FX stage). Making it
 * a distinct type from ChainElement encodes those invariants structurally -
 * there is no "is post-fx" flag and no runtime placement check; the type system
 * simply cannot represent a rack or a nested structure here.
 */
struct PostFxChainElement {
    DeviceInfo device;
};

/**
 * @brief The full signal chain of a track, split into the main FX chain and a
 *        flat post-FX stage.
 *
 * - fxChainElements:     the main FX chain. A full device/rack tree
 *                        (ChainElement), reusing all existing nesting,
 *                        deep-copy, sync-flatten and path-resolution machinery.
 * - postFxChainElements: the post-FX stage. A flat list of devices that runs
 *                        after the main FX chain but still before the fader
 *                        (post-FX, not post-fader).
 *
 * The track fader (VolumeAndPan) is not modelled as a node. The intended
 * routing (sync not yet implemented) is:
 *   flatten(fxChainElements) -> postFxChainElements -> VolumeAndPan -> LevelMeter
 */
struct TrackChain {
    std::vector<ChainElement> fxChainElements;            // main / pre-fader (tree)
    std::vector<PostFxChainElement> postFxChainElements;  // post-fader (flat)

    // Rail-managed analysis devices (mini Oscilloscope / Spectrum on the
    // mixer). Same shape as post-FX devices but populated by the mixer rail
    // toggle, not by the user — kept separate so the two never confuse each
    // other, and skipped at serialization (session-only state, restored from
    // the rail toggle in Config).
    std::vector<PostFxChainElement> mixerAnalysisElements;

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
        postFxChainElements = other.postFxChainElements;
        mixerAnalysisElements = other.mixerAnalysisElements;
    }
};

}  // namespace magda
