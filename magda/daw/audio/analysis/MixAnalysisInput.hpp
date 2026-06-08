#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <string>
#include <vector>

#include "../../../agents/mixing_agent.hpp"

namespace magda::daw::audio {

/**
 * @brief Builds a MixAnalysisAgent::Input from audio buffers (#886).
 *
 * Measures each track + the master + any reference masters with the production
 * analysis DSP (TrackMeasurer / BandSpectrum / MaskingDetector), detects
 * inter-track masking, and slices a master timeline -- the exact pipeline the
 * exploratory harness validated, lifted into a reusable module so the UI
 * (offline-rendered stems) and tests (loaded files) share one implementation.
 *
 * The caller owns the audio buffers (rendered stems / loaded files) and fills
 * song-level context (bpm / genre / question) on the returned Input afterwards.
 * All processing is offline / synchronous; run it off the message thread.
 */
class MixAnalysisInput {
  public:
    /// One audio source: a track stem, the master, or a reference. The buffer is
    /// borrowed (mono or stereo); name/role are how the model refers to it.
    struct Source {
        juce::String name;
        std::string role;                                 // optional ("kick", "vocal", ...)
        const juce::AudioBuffer<float>* audio = nullptr;  // non-owning; mono or stereo
    };

    struct Options {
        int numSegments = 16;  ///< master timeline slices (fixed windows)
    };

    /**
     * Build the agent input.
     * @param sampleRate  applies to every buffer (they must share it).
     * @param tracks      every track stem.
     * @param master      the finished master; nullptr => a normalised (-1 dBFS)
     *                    sum of the track stems is used instead.
     * @param references  reference masters (genre targets), may be empty.
     */
    static MixAnalysisAgent::Input build(double sampleRate, const std::vector<Source>& tracks,
                                         const juce::AudioBuffer<float>* master,
                                         const std::vector<Source>& references,
                                         const Options& opts);

    /// Convenience overload with default options.
    static MixAnalysisAgent::Input build(double sampleRate, const std::vector<Source>& tracks,
                                         const juce::AudioBuffer<float>* master,
                                         const std::vector<Source>& references) {
        return build(sampleRate, tracks, master, references, Options{});
    }

    /// Master-style fingerprint of one finished buffer (true-peak + tonal +
    /// spectral + whole-song correlation/width).
    static MixAnalysisAgent::TrackMix fingerprint(const juce::AudioBuffer<float>& buf,
                                                  double sampleRate, const juce::String& name,
                                                  const std::string& role);
};

}  // namespace magda::daw::audio
