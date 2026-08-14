#pragma once

#include <vector>

#include "DeviceInfo.hpp"
#include "RackInfo.hpp"  // ChainElement, deepCopyElement

namespace magda {

/**
 * @brief A single element in a track's post-FX chain.
 *
 * Post-FX is flat by design: a linear list of effect / analysis devices that
 * run after the main FX chain. It is never an instrument (nothing generates
 * sound at this stage) and never a rack (no parallel routing in the post-FX
 * stage). Making it a distinct type from ChainElement encodes those invariants
 * structurally - there is no "is post-fx" flag and no runtime placement check;
 * the type system simply cannot represent a rack or a nested structure here.
 *
 * These are ordinary devices: they process audio and they render. The section
 * exists for the things #730 asked for - dithering, final limiting, metering -
 * so "does nothing to the signal" was never what post-FX meant.
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
 *                        after the main FX chain, on whichever side of the
 *                        fader `postFxPostFader` says.
 *
 * The track fader (VolumeAndPan) is not modelled as a node. The routing is:
 *
 *   post-fader (default):
 *     flatten(fx) -> VolumeAndPan -> postFx -> mixerAnalysis -> LevelMeter
 *   pre-fader:
 *     flatten(fx) -> postFx -> mixerAnalysis -> VolumeAndPan -> LevelMeter
 */
struct TrackChain {
    std::vector<ChainElement> fxChainElements;            // main insert chain (tree)
    std::vector<PostFxChainElement> postFxChainElements;  // post-FX stage (flat)

    /**
     * Which side of the track fader the post-FX stage sits on (#2087).
     *
     * The name "post-FX" has always meant *after the FX chain*; where it sat
     * relative to the fader was left open. #1335 shipped it pre-fader and
     * `de7a0b7c` recorded the fader boundary as deferred, but the panel and
     * several comments went on calling the section post-fader, so the two
     * readings have been live side by side ever since. This is the flag that
     * decides, rather than an ordering each engine hardcodes and drifts on.
     *
     * Default post-fader, which is what the section is called and what makes a
     * meter dropped in here read the signal leaving the track. A project that
     * wants its post-FX processing to feed the fader turns it off, one switch
     * for the whole chain. Per-device placement is the v1 upgrade; the
     * compilers already partition on a value rather than on a constant, so it
     * is a widening rather than a rewrite.
     */
    bool postFxPostFader = true;

    // Chain power (the track chain header's power button + the track
    // inspector's enable switch). When off, every insert-chain device is
    // gated off in the engine WITHOUT touching the devices' own bypassed
    // flags, so per-device bypass states survive an off/on cycle. Post-FX
    // and mixer-analysis devices sit outside the insert chain and are not
    // gated.
    bool enabled = true;

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
        enabled = other.enabled;
        postFxPostFader = other.postFxPostFader;
    }
};

}  // namespace magda
