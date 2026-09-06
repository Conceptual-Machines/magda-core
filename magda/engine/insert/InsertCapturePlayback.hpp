#pragma once

#include <memory>
#include <vector>

#include "exec/EngineDevice.hpp"
#include "insert/InsertCapture.hpp"

/**
 * @file InsertCapturePlayback.hpp
 * @brief A capture standing in for the hardware, for one render (#2279).
 *
 * The other implementation of the insert seam: the same two calls, answered
 * from a recording rather than from an interface, which is what lets a bounce
 * run faster than the outside world (exec/EngineDevice.hpp).
 *
 * It constructs only when the capture can actually serve the render, so there
 * is no flag anyone has to check first: a render that cannot be served has
 * nothing to bind, and the failure is in front of whoever was about to start it
 * rather than inside a file nobody listens to afterwards.
 */

namespace magda::engine {

class InsertCapturePlayback final : public EngineInsert {
  public:
    /**
     * @brief A playback of @p capture over @p window for @p context, or null.
     *
     * Null when the capture is not complete, when it does not cover @p window,
     * when it has fewer channels than the render, or when there is nothing in
     * it. Each of those is a render that would otherwise write plausible wrong
     * audio into a file.
     *
     * A capture taken at another rate is resampled here rather than refused,
     * which is the incumbent's behaviour: `InsertRenderCaptureService` records
     * at the device's rate and resamples on completion, and an export at 96 kHz
     * of a session tracked at 44.1 must not need a second live pass. The curve
     * is the one a file at another rate is read through (io/SourceReaders.hpp),
     * so nothing is resampled to a quality a listener could name.
     *
     * Off the audio thread: this allocates and resamples.
     */
    static std::unique_ptr<InsertCapturePlayback> create(const InsertCapture& capture,
                                                         const CaptureWindow& window,
                                                         const RenderContext& context);

    /**
     * @brief The round trip the live pass had, at the render's rate.
     *
     * The same number the hardware reported, because the capture holds what
     * came back at the moment it came back: replaying it puts the material
     * exactly where the live pass put it, and the compensation that lined the
     * pass up lines the bounce up too.
     */
    int latencySamples() const override {
        return latencySamples_;
    }

    /// Nothing leaves the machine during a bounce. The block is not looked at.
    void send(const BlockInfo&, juce::dsp::AudioBlock<const float>,
              const juce::MidiBuffer&) override {}

    void receive(const BlockInfo& block, juce::dsp::AudioBlock<float> audio,
                 juce::MidiBuffer& midi) override;

  private:
    InsertCapturePlayback(juce::AudioBuffer<float> audio, std::vector<CapturedMidiEvent> midi,
                          CaptureWindow window, double sampleRate, int latencySamples);

    juce::AudioBuffer<float> audio_;

    /**
     * @brief The capture's messages, sorted by sample, at the render's rate.
     *
     * Sorted so that a block finds its own events by a search rather than a walk.
     */
    std::vector<CapturedMidiEvent> midi_;

    CaptureWindow window_;
    double sampleRate_ = 0.0;
    int latencySamples_ = 0;
};

}  // namespace magda::engine
