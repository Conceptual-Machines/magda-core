#pragma once

#include <array>
#include <atomic>
#include <bitset>
#include <vector>

#include "exec/EngineDevice.hpp"
#include "io/LiveInput.hpp"
#include "io/LiveOutput.hpp"

/**
 * @file LiveInsert.hpp
 * @brief A hardware insert running against the audio device (#2279).
 *
 * The insert seam's live implementation (exec/EngineDevice.hpp): the send is
 * added to the callback's output channels or sent to a MIDI port, the return is
 * read from its input channels. Which channels and which port a device name
 * means is the host's to resolve.
 */

namespace magda::engine {

/** @brief Where one insert's two ends are on the device, resolved by the host. */
struct LiveInsertRoute {
    /// Callback output channels the send is added to. Two carry left and right; one
    /// carries the left, as a mono track output does. Empty sends no audio.
    std::vector<int> sendChannels;

    /// Where a MIDI send goes, or null for none. Owned by the host, and alive for as
    /// long as the insert.
    LiveMidiOutput* midi = nullptr;

    /// Callback input channels the return reads. A mono return fills both sides.
    std::vector<int> returnChannels;

    /// The round trip, device buffering and the manual correction included.
    int latencySamples = 0;
};

class LiveInsert final : public EngineInsert {
  public:
    LiveInsert(const LiveInputFeed& inputs, LiveOutputFeed& outputs, LiveInsertRoute route);

    /// Releases every note this sent and never ended.
    ~LiveInsert() override;

    int latencySamples() const override {
        return route_.latencySamples;
    }

    void send(const BlockInfo& block, juce::dsp::AudioBlock<const float> audio,
              const juce::MidiBuffer& midi) override;

    void receive(const BlockInfo& block, juce::dsp::AudioBlock<float> audio,
                 juce::MidiBuffer& midi) override;

    void releaseNotes(const BlockInfo& block) override;

    /// Blocks whose send found no output channel it named. Read from any thread.
    std::uint32_t missingSendBlocks() const {
        return missingSend_.load(std::memory_order_relaxed);
    }

  private:
    void track(const juce::MidiMessageMetadata& event);

    LiveOutputFeed& outputs_;
    LiveInsertRoute route_;
    LiveAudioInput return_;

    /// Notes sent and not yet ended, by channel. Audio thread, and the destructor
    /// once nothing renders through this.
    std::array<std::bitset<128>, 16> held_{};

    std::atomic<std::uint32_t> missingSend_{0};
};

}  // namespace magda::engine
