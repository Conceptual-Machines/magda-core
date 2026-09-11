#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "../core/DeviceInfo.hpp"

/**
 * @file FourOscTranslation.hpp
 * @brief A saved 4OSC patch as a Poly Synth patch (#2437).
 *
 * 4OSC is Tracktion Engine's own synth, so it goes when the fork goes (#2298)
 * and is never ported. A project holding one still has to open on the native
 * engine, and this is what it opens as.
 */

namespace magda::daw::audio {

/** @brief What a translated patch could not carry, for the caller to report. */
struct FourOscGap {
    /// The 4OSC control, named as its UI names it.
    juce::String control;

    /// What happens instead.
    juce::String effect;
};

/** @brief A translated patch and everything Poly Synth has no place for. */
struct FourOscTranslation {
    DeviceInfo device;

    /// 4OSC's built-in effects, as MAGDA devices in one rack, in the order
    /// 4OSC processes them. Null when the patch had none switched on.
    std::unique_ptr<RackInfo> effects;

    std::vector<FourOscGap> gaps;
};

/// Whether @p device is a 4OSC instance, by the pluginId a project saves.
bool isFourOscDevice(const DeviceInfo& device);

/**
 * @brief Translate @p fourOsc into a Poly Synth device.
 *
 * Carries the four oscillators, the filter and both envelopes, voice mode,
 * glide and the velocity amounts. Identity stays @p fourOsc's: the same
 * DeviceId, the same place in the chain, so links and automation still name
 * this device.
 *
 * 4OSC's built-in effects become MAGDA devices in `effects`, so @p nextEffectId
 * has to hand out ids the project is not already using. Passing nothing skips
 * them, which is what a caller only asking what a patch would lose wants.
 *
 * A control Poly Synth does not have is listed in `gaps` rather than
 * approximated. Unison is the loudest of those: a patch built on detuned
 * voices translates to a thinner sound and the caller has to say so.
 */
FourOscTranslation translateFourOsc(const DeviceInfo& fourOsc,
                                    const std::function<DeviceId()>& nextEffectId = nullptr);

}  // namespace magda::daw::audio
