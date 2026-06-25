#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "plugins/MidiDevicePlugin.hpp"

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
 * (Sync mode) - unlike the old in-instrument path's fixed 6 ms gate, which only
 * worked because struck/plucked voices decay on their own.
 */
class MidiStrumPlugin : public MidiDevicePlugin {
  public:
    MidiStrumPlugin(const te::PluginCreationInfo& info);
    ~MidiStrumPlugin() override;

    static const char* getPluginName() {
        return "Strum";
    }
    static const char* xmlTypeName;

    enum class Trigger { Chord = 0, Sync };
    enum class Order { Up = 0, Down, UpDown, AsPlayed };

    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "Strum";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

    void initialise(const te::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void reset() override;
    void applyToBuffer(const te::PluginRenderContext&) override;
    void restorePluginStateFromValueTree(const juce::ValueTree&) override;

    // CachedValues (persistence) + AutomatableParameters (macro/mod linking).
    juce::CachedValue<int> trigger, order, shape, cycles;
    juce::CachedValue<float> strumLength, syncInterval;
    te::AutomatableParameter::Ptr triggerParam, orderParam, shapeParam, cyclesParam;
    te::AutomatableParameter::Ptr strumLengthParam, syncIntervalParam;

  private:
    struct Held {
        int note = 0;
        int velocity = 0;
        std::int64_t order = 0;
    };
    struct Pending {
        std::int64_t fireAt = 0;  // absolute sample clock
        int note = 0;
        int velocity = 0;    // 0..127
        bool gateOn = true;  // true = note-on, false = note-off
    };

    void scheduleStrum();       // queue note-ons (+ Sync re-strum note-offs)
    void scheduleReleaseAll();  // queue note-offs for everything sounding (at clock_)
    void resetStrumState();
    float controlValue(te::AutomatableParameter* p, const juce::CachedValue<float>& cv) const;
    int controlIndex(te::AutomatableParameter* p, const juce::CachedValue<int>& cv) const;

    std::vector<Held> held_;
    std::vector<Pending> pending_;
    std::vector<int> sounding_;      // notes we have emitted note-on for, awaiting release
    std::int64_t clock_ = 0;         // absolute sample counter
    std::int64_t noteOrder_ = 0;     // play-order stamp for As-Played ordering
    int collectLeft_ = -1;           // Chord-mode collect debounce (samples)
    int syncLeft_ = 0;               // Sync-mode interval countdown (samples)
    int lutShape_ = -1;              // shape index the LUT was built for
    std::array<float, 1024> lut_{};  // current strum curve, sampled

    void syncParamFromProperty(const juce::Identifier& property);
    struct ParamSyncListener : public juce::ValueTree::Listener {
        MidiStrumPlugin& owner;
        explicit ParamSyncListener(MidiStrumPlugin& o) : owner(o) {}
        void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier& p) override {
            owner.syncParamFromProperty(p);
        }
    };
    ParamSyncListener paramSyncListener_{*this};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiStrumPlugin)
};

}  // namespace magda::daw::audio
