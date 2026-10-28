// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <magda/sdk/state/StateNode.hpp>
#include <map>
#include <memory>
#include <vector>

#include "plugins/DeviceJuceInterop.hpp"
#include "plugins/sampler/SamplerVoice.hpp"
#include "plugins/sampler/SamplerZones.hpp"

namespace magda::daw::audio {

/**
 * @brief The Sampler's playing half, for any device built on it (#3100): its zones and their
 *        audio, the voices, and a block's MIDI.
 *
 * The device keeps its own parameters and turns them into SamplerVoiceSettings each block it
 * renders. A device with its own voice type passes a factory that makes them.
 *
 * Zones are authored on the message thread and published whole to the audio thread
 * (SamplerPlayback); a file is read once and kept while a zone names it (#2379).
 */
class SamplerEngine {
  public:
    using VoiceFactory = std::function<std::unique_ptr<SamplerVoice>(const SamplerSynth&)>;

    explicit SamplerEngine(int voices = 8, const VoiceFactory& makeVoice = {});
    ~SamplerEngine();

    void prepare(double sampleRate);
    /// Every voice silenced at once.
    void stopAll();

    /**
     * @brief Render one block into @p audio, which it adds to.
     *
     * @p settings is called only when something will sound, for the voices' settings: a block
     * with no voice ringing and no MIDI arriving costs nothing (a Drum Grid holds dozens of
     * silent pads). Audio thread.
     */
    template <typename Settings>
    void process(juce::AudioBuffer<float>& audio, const DeviceMidiInput* in, int numSamples,
                 Settings&& settings) {
        if (!beginBlock(in))
            return;
        synth_.apply(settings());
        render(audio, in, numSamples);
    }

    //==============================================================================
    // Zones. Message thread.

    /// Every zone, as the audio thread plays them.
    const SamplerZoneMap& zoneMap() const {
        return playback_.current();
    }
    /// The authored zones; a zone whose file is missing keeps its path.
    const std::vector<SamplerZoneSettings>& zones() const {
        return zones_;
    }
    /// Whether the zones are a list, each with its own markers, rather than the one zone whose
    /// markers are the device's parameters.
    bool hasZoneList() const {
        return zoneList_;
    }
    void setZones(std::vector<SamplerZoneSettings> zones, bool list);

    /// A file's audio, read again only when the file changed since. Null when no sample format
    /// can read it.
    std::shared_ptr<const SampleAudio> audioFor(const juce::String& path);

    /// True when the one zone holds the audio of the file at @p path, still as it was when read.
    /// The path alone cannot tell a re-projection of the same document from a file replaced in
    /// place (#2379).
    bool holdsAudioFrom(const juce::String& path) const;

    /// One zone off a state node: a `zone` child, or the root of a one-zone sampler.
    static SamplerZoneSettings zoneFromState(const sdk::StateNode& node);
    /// The document's `zone` children; empty for a one-zone sampler.
    static std::vector<SamplerZoneSettings> listedZones(const sdk::StateNode& state);

    //==============================================================================
    /// Where in its sample the first sounding voice is, in seconds, and its zone or -1. Written
    /// by the audio thread, read by the UI.
    double playbackPosition() const {
        return playbackPosition_.load(std::memory_order_relaxed);
    }
    int playbackZone() const {
        return playbackZone_.load(std::memory_order_relaxed);
    }

    SamplerSynth& synth() {
        return synth_;
    }

  private:
    /// The block's map to the synthesiser; false, with the map acknowledged, when nothing will
    /// sound.
    bool beginBlock(const DeviceMidiInput* in);
    void render(juce::AudioBuffer<float>& audio, const DeviceMidiInput* in, int numSamples);
    void publishZones();

    SamplerSynth synth_;
    SamplerPlayback playback_;
    double sampleRate_ = 44100.0;

    /// Where each event handed to the synthesiser falls inside its sample (#2741). Reserved
    /// once, never grown on the audio thread.
    std::vector<float> eventFractions_;
    /// A note edge already handed over this block, for the dedup in render().
    struct SeenEvent {
        int note;
        int samplePos;
        float fraction;
        bool isNoteOn;
        bool operator==(const SeenEvent&) const = default;
    };
    static constexpr int kMaxBlockEvents = 1024;
    /// Past this many note edges in one block, later duplicates are no longer caught.
    static constexpr int kMaxSeenEdges = 256;
    std::vector<SeenEvent> seenEvents_;
    juce::MidiBuffer blockMidi_;
    /// Whether a voice was still sounding when the last block ended. Audio thread only.
    bool sounding_ = false;
    const SamplerZoneMap* blockMap_ = nullptr;

    std::atomic<double> playbackPosition_{0.0};
    std::atomic<int> playbackZone_{-1};

    std::vector<SamplerZoneSettings> zones_;
    bool zoneList_ = false;
    struct LoadedAudio {
        std::shared_ptr<const SampleAudio> audio;
        juce::int64 size = 0;
        juce::int64 modifiedMs = 0;
    };
    std::map<juce::String, LoadedAudio> loaded_;

    /// Frees replaced maps once the audio thread has let go of them; runs only while some wait.
    class RetireTimer : public juce::Timer {
      public:
        explicit RetireTimer(SamplerPlayback& playback) : playback_(playback) {}
        void timerCallback() override {
            playback_.releaseRetired();
            if (playback_.retiredCount() == 0)
                stopTimer();
        }

      private:
        SamplerPlayback& playback_;
    };
    RetireTimer retireTimer_{playback_};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerEngine)
};

}  // namespace magda::daw::audio
