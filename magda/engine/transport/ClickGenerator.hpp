#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "exec/RenderContext.hpp"
#include "transport/TransportState.hpp"

/**
 * @file ClickGenerator.hpp
 * @brief The metronome.
 *
 * Not an op, and deliberately not in the plan. The click is not part of the
 * model's signal graph: it is never recorded, never routed, and never touched
 * by the master fader, so putting it in the plan would mean recompiling the
 * graph to toggle it and finding somewhere to hide it from every render. It is
 * summed into the output after the plan instead, which is where the incumbent
 * puts it too.
 *
 * The sounds are synthesised once when the generator is prepared, so the audio
 * thread only ever copies. Two of them: a bar accent and a beat.
 */

namespace magda::engine {

class ClickGenerator {
  public:
    /// Off the audio thread. A generator is prepared once for a device and
    /// carried across plan swaps, so a click that is sounding stays sounding.
    void prepare(const RenderContext& context);

    /**
     * @brief Add this block's clicks to @p output.
     *
     * On the audio thread. @p countingIn sounds the metronome whether or not it
     * is switched on: a count-in that did not count would just be a late start.
     *
     * A click outlives the block it starts in, so what is left of one carries
     * to the next block; that is the only state here, and a locate does not
     * clear it, for the same reason a locate does not clear a reverb tail.
     */
    void render(const TempoMap& tempo, const ClickSettings& click, const BlockInfo& block,
                bool countingIn, juce::AudioBuffer<float>& output, int startSample);

  private:
    void trigger(bool accent);

    /// Copy what is left of the sounding click into the block, from
    /// @p startSample, and advance by however much fitted.
    void pour(juce::AudioBuffer<float>& output, int startSample, int numSamples, float gain);

    juce::AudioBuffer<float> barClick_, beatClick_;

    /// The click that is sounding, and how far into it we are. Null between
    /// clicks, which is most of the time.
    const juce::AudioBuffer<float>* sounding_ = nullptr;
    int soundingPosition_ = 0;
};

}  // namespace magda::engine
