#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include "exec/EngineDevice.hpp"
#include "insert/InsertCapture.hpp"

/**
 * @file InsertCaptureSession.hpp
 * @brief The live pass that writes a capture (#2279).
 *
 * Stands where the insert stands during an ordinary playback pass: every send
 * and receive is forwarded to the hardware, so the pass sounds exactly as it
 * would without this in the way, and what comes back is written down.
 *
 * The only thing in the capture path that touches hardware and the only thing
 * that writes. Everything downstream reads an InsertCapture, which cannot be
 * written to at all.
 */

namespace magda::engine {

class InsertCaptureSession final : public EngineInsert {
  public:
    /**
     * @brief Capture @p live over @p window, at @p sampleRate.
     *
     * @p live is the insert the pass actually runs through and is owned by the
     * caller, which must outlive this.
     *
     * Room for the whole window is taken here, so the write path never
     * allocates: the audio is the window at @p sampleRate and @p numChannels,
     * and the MIDI is @p midiCapacity events, defaulted from the window.
     *
     * In memory rather than on disk, which is where the incumbent puts it. A
     * minute of stereo at 44.1 kHz is 21 MB, and what a disk-backed capture
     * needs -- a FIFO and a writer thread off the audio thread -- is what
     * recording (#1895) is about to build. It goes through here when it exists.
     */
    InsertCaptureSession(EngineInsert& live, const CaptureWindow& window, double sampleRate,
                         int numChannels, int midiCapacity = 0);

    /**
     * @brief Room for a window's MIDI, in events.
     *
     * A DIN return cannot deliver more: 31250 baud at ten bits a byte is about
     * 1041 three-byte messages a second, so 1100 a second is past what the wire
     * can carry, and the floor covers a window too short to matter.
     */
    static int defaultMidiCapacity(const CaptureWindow& window);

    // --- EngineInsert, all of it forwarded ---

    void prepare(const RenderContext& context) override;
    void reset() override;
    int latencySamples() const override;
    void send(const BlockInfo& block, juce::dsp::AudioBlock<const float> audio,
              const juce::MidiBuffer& midi) override;
    void receive(const BlockInfo& block, juce::dsp::AudioBlock<float> audio,
                 juce::MidiBuffer& midi) override;

    /// What the pass wrote, as a capture. Off the audio thread, after the pass.
    InsertCapture take() const;

    /**
     * @brief Samples of the window nothing has written yet.
     *
     * A counter kept as the writes happen rather than a scan, so a progress
     * display on another thread costs the pass nothing. Zero is a window
     * covered; anything else is a capture that will not be complete.
     */
    std::int64_t missingSamples() const;

  private:
    /// Mark [@p from, @p to) written, and count only what was not already.
    void markCovered(std::int64_t from, std::int64_t to);

    EngineInsert& live_;
    CaptureWindow window_;
    double sampleRate_ = 0.0;

    juce::AudioBuffer<float> audio_;

    std::vector<CapturedMidiEvent> midi_;
    int midiCount_ = 0;

    /**
     * @brief Which samples of the window have been written.
     *
     * A bit each rather than a list of ranges: a pass that seeks over the
     * window leaves holes, and how many holes is up to the person driving the
     * transport, while a bitmap's cost is the window's length and known before
     * the pass starts.
     */
    std::vector<bool> covered_;

    /// Read from another thread, so an atomic. Written only by the audio thread.
    std::atomic<std::int64_t> writtenSamples_{0};

    std::atomic<bool> midiOverflowed_{false};
};

}  // namespace magda::engine
