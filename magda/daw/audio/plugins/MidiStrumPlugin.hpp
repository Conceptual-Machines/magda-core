#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "core/ParameterUtils.hpp"
#include "plugins/MidiMagdaDevice.hpp"

namespace magda::daw::audio {

/**
 * @brief MIDI strum effect: turns a held chord into a curve-shaped strum / roll.
 *
 * Sibling to the Arpeggiator. Placed before any instrument (Pluck, Percussion,
 * PolySynth, external VST/AU), it latches the held chord and re-emits its notes
 * at curve-shaped onsets so the chord is strummed, rolled or arpeggiated in
 * time. The scheduler (chord latch + cubic-bezier shape LUT) is the one that
 * used to live inside MagdaStrumInstrument; here it emits timestamped MIDI
 * instead of driving a voice allocator, so it works with any downstream
 * instrument.
 *
 * Notes ring until the chord is released (Chord mode) or the next re-strum
 * (Loop mode) - unlike the old in-instrument path's fixed 6 ms gate, which only
 * worked because struck/plucked voices decay on their own. The Loop interval is
 * either a free millisecond value or tempo-locked to a beat division.
 *
 * A MagdaDevice since #2299: one scheduler hosted by whichever engine is
 * running it. The slot ids, order and display ranges are the ones the retired
 * host-native plugin registered.
 */
class MidiStrumPlugin : public MidiMagdaDevice {
  public:
    MidiStrumPlugin();
    ~MidiStrumPlugin() override;

    static const char* getPluginName() {
        return "Strum";
    }
    static const char* xmlTypeName;

    enum class Trigger { Chord = 0, Loop };
    enum class Order { Up = 0, Down, UpDown, AsPlayed };
    // Loop-interval clock source: a free millisecond value, or tempo-locked to a
    // musical division of the host beat.
    enum class LoopSync { Time = 0, Beat };
    // Beat divisions for tempo-locked Loop mode (index order == UI/param order).
    static double loopRateToBeats(int rateIndex);
    static constexpr int kNumLoopRates = 8;
    static constexpr int kNumShapes = 8;

    /// FROZEN slot order - saved links address these by index.
    enum ParamIndex {
        kTrigger = 0,
        kOrder,
        kShape,
        kCycles,  // 0..7 -> 1..8 mini-strums
        kLoopSync,
        kLoopRate,
        kStrumLength,   // ms
        kSyncInterval,  // ms (Time mode loop interval)
        kNumParams
    };

    DeviceProperties properties() const override {
        return {
            .pluginId = xmlTypeName,
            .name = getPluginName(),
            .shortName = "Strum",
            .takesMidiInput = true,
        };
    }

    void prepare(const DevicePrepareContext& context) override;
    void reset() override;
    void process(DeviceProcessContext& context) override;

    int parameterCount() const override {
        return kNumParams;
    }
    ParameterInfo parameterInfo(int index) const override;
    float parameterValue(int index) const override;
    void setParameterValue(int index, float value) override;

    /// UI viz helper: normalized onset times [0,1] for `count` evenly-spaced
    /// notes with the given Shape preset and Cycles index. Pure (its own local
    /// LUT), so the message thread can call it without touching the device.
    static std::vector<float> curveOnsetPreview(int shapeIndex, int cyclesIndex, int count);

  private:
    struct Held {
        int note = 0;
        int velocity = 0;
        std::int64_t order = 0;
        /// Who played it, carried onto what this note strums. A device behind
        /// this one reads provenance the same way, and the source is all of it
        /// the host's buffer carries between them (#2416).
        std::uint32_t sourceId = 0;
    };
    struct Pending {
        std::int64_t fireAt = 0;  // absolute sample clock
        int note = 0;
        int velocity = 0;    // 0..127
        bool gateOn = true;  // true = note-on, false = note-off
        std::uint32_t sourceId = 0;
    };
    struct Sounding {
        int note = 0;
        std::uint32_t sourceId = 0;
    };

    void scheduleStrum();       // queue note-ons (+ Loop re-strum note-offs)
    void scheduleReleaseAll();  // queue note-offs for everything sounding (at clock_)
    void resetStrumState();
    // Loop re-strum interval in samples: either the free ms value or a tempo-
    // locked beat division (read from the block's tempo map).
    int loopIntervalSamples(const DeviceProcessContext& context) const;

    /// The parameter's display-domain value, converted through the cached
    /// domain rather than a freshly built ParameterInfo: this runs per block on
    /// the audio thread, and a ParameterInfo carries strings that allocate.
    float displayValue(int index) const;
    int displayIndex(int index) const;

    std::array<float, kNumParams> values_{};
    std::array<ParameterUtils::ParameterDomain, kNumParams> domains_{};

    std::vector<Held> held_;
    std::vector<Pending> pending_;
    std::vector<Pending> due_;        // scratch for the per-block emit pass
    std::vector<Sounding> sounding_;  // notes we have emitted note-on for, awaiting release
    std::int64_t clock_ = 0;          // absolute sample counter
    std::int64_t noteOrder_ = 0;      // play-order stamp for As-Played ordering
    int collectLeft_ = -1;            // Chord-mode collect debounce (samples)
    int syncLeft_ = 0;                // Loop-mode interval countdown (samples)
    int lutShape_ = -1;               // shape index the LUT was built for
    std::array<float, 1024> lut_{};   // current strum curve, sampled
    bool wasPlaying_ = false;         // transport state last block (stop -> flush)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiStrumPlugin)
};

}  // namespace magda::daw::audio
