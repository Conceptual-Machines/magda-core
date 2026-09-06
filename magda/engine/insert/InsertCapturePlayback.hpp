#pragma once

#include <memory>
#include <vector>

#include "exec/EngineDevice.hpp"
#include "insert/InsertCapture.hpp"

/**
 * @file InsertCapturePlayback.hpp
 * @brief A capture standing in for the hardware, for one render (#2279).
 *
 * The insert seam's other implementation, which is what lets a bounce outrun
 * the outside world (exec/EngineDevice.hpp).
 */

namespace magda::engine {

class InsertCapturePlayback final : public EngineInsert {
  public:
    /**
     * @brief A playback of @p capture over @p window for @p context, or null.
     *
     * Null for a capture that is incomplete, does not cover @p window, or is
     * narrower than the render. Failing here rather than carrying a flag is the
     * point (#2279): a render that cannot be served has nothing to bind.
     *
     * Another rate is resampled rather than refused, so an export at 96 kHz of
     * a session tracked at 44.1 needs no second live pass. The curve is the one
     * io/SourceReaders.hpp reads a file at another rate through. Off the audio
     * thread: this allocates.
     */
    static std::unique_ptr<InsertCapturePlayback> create(const InsertCapture& capture,
                                                         const CaptureWindow& window,
                                                         const RenderContext& context);

    /// The round trip the live pass had, at the render's rate. The capture holds
    /// what came back when it came back, so the same compensation lines both up.
    int latencySamples() const override {
        return latencySamples_;
    }

    /// Nothing leaves the machine during a bounce.
    void send(const BlockInfo&, juce::dsp::AudioBlock<const float>,
              const juce::MidiBuffer&) override {}

    void receive(const BlockInfo& block, juce::dsp::AudioBlock<float> audio,
                 juce::MidiBuffer& midi) override;

  private:
    InsertCapturePlayback(juce::AudioBuffer<float> audio, std::vector<CapturedMidiEvent> midi,
                          CaptureWindow window, double sampleRate, int latencySamples);

    juce::AudioBuffer<float> audio_;

    /// Sorted, at the render's rate, so a block finds its events by a search.
    std::vector<CapturedMidiEvent> midi_;

    CaptureWindow window_;
    double sampleRate_ = 0.0;
    int latencySamples_ = 0;
};

}  // namespace magda::engine
