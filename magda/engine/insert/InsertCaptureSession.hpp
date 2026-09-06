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
 * Stands where the insert stands and forwards every call to it, so the pass
 * sounds as it would without this in the way. The only thing here that touches
 * hardware and the only thing that writes.
 */

namespace magda::engine {

class InsertCaptureSession final : public EngineInsert {
  public:
    /**
     * @brief Capture @p live over @p window, at @p sampleRate.
     *
     * @p live is owned by the caller and must outlive this. Room for the whole
     * window is taken here, so the write path never allocates.
     *
     * In memory rather than on disk: a disk-backed capture needs the FIFO and
     * writer thread recording (#1895) is about to build, and goes through here
     * when they exist.
     */
    InsertCaptureSession(EngineInsert& live, const CaptureWindow& window, double sampleRate,
                         int numChannels, int midiCapacity = 0);

    /**
     * @brief Room for a window's MIDI, in events.
     *
     * A DIN return cannot deliver more: 31250 baud at ten bits a byte is about
     * 1041 three-byte messages a second.
     */
    static int defaultMidiCapacity(const CaptureWindow& window);

    void prepare(const RenderContext& context) override;
    void reset() override;
    int latencySamples() const override;
    void send(const BlockInfo& block, juce::dsp::AudioBlock<const float> audio,
              const juce::MidiBuffer& midi) override;
    void receive(const BlockInfo& block, juce::dsp::AudioBlock<float> audio,
                 juce::MidiBuffer& midi) override;

    /** @brief What the pass wrote. Off the audio thread, after the pass. */
    InsertCapture take() const;

    /**
     * @brief Samples of the window nothing has written yet.
     *
     * A counter rather than a scan, so reading it from another thread costs the
     * pass nothing.
     */
    std::int64_t missingSamples() const;

  private:
    /** @brief Stamp [@p from, @p to) with this write, counting what was new. */
    void markCovered(std::int64_t from, std::int64_t to);

    /**
     * @brief Drop the events a later write has replaced.
     *
     * In place and only when the room runs out, so a pass that loops for an
     * hour is not held to the events of the passes it overwrote.
     */
    void compactMidi();

    EngineInsert& live_;
    CaptureWindow window_;
    double sampleRate_ = 0.0;

    juce::AudioBuffer<float> audio_;

    std::vector<CapturedMidiEvent> midi_;

    /// The write each event arrived in, so one a later pass replaced can be
    /// told from one it kept. Beside the events rather than in them: which pass
    /// wrote a message is the session's business, not the capture's.
    std::vector<std::uint32_t> midiWrites_;

    int midiCount_ = 0;

    /**
     * @brief The write that last covered each sample, and zero for never.
     *
     * Per sample rather than a list of ranges: a pass that seeks leaves as many
     * holes as the transport was driven to make. Numbered rather than flagged
     * so a revisited stretch takes its MIDI with it -- audio is overwritten by
     * the copy, and an event has to be dropped by something.
     */
    std::vector<std::uint32_t> writtenBy_;

    /// Counts up per write. Zero is never written, so this starts at one.
    std::uint32_t writeId_ = 0;

    /// Read from another thread; written only by the audio thread.
    std::atomic<std::int64_t> writtenSamples_{0};

    std::atomic<bool> midiOverflowed_{false};

    /// A return with fewer channels than the capture owns. Allocated width is
    /// not recorded width, and the difference would export as silence.
    std::atomic<bool> narrowReturn_{false};
};

}  // namespace magda::engine
