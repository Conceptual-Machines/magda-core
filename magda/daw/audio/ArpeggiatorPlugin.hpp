#pragma once

#include <array>

#include "MidiDevicePlugin.hpp"

namespace magda::daw::audio {

/**
 * @brief MIDI arpeggiator plugin that transforms held notes into rhythmic patterns.
 *
 * Placed on a track's FX chain before a synth. Captures incoming MIDI note-on/off
 * events, clears the MIDI buffer, and outputs arpeggiated notes synced to the
 * edit's tempo. All processing happens on the audio thread.
 */
class ArpeggiatorPlugin : public MidiDevicePlugin {
  public:
    ArpeggiatorPlugin(const te::PluginCreationInfo& info);
    ~ArpeggiatorPlugin() override;

    static const char* getPluginName() {
        return "Arpeggiator";
    }
    static const char* xmlTypeName;

    // --- Enums ---
    enum class Pattern { Up = 0, Down, UpDown, DownUp, Random, AsPlayed };
    enum class Rate {
        DottedQuarter = 0,
        Quarter,
        TripletQuarter,
        DottedEighth,
        Eighth,
        TripletEighth,
        DottedSixteenth,
        Sixteenth,
        TripletSixteenth,
        ThirtySecond
    };
    enum class VelocityMode { Original = 0, Fixed, Accent };

    // --- te::Plugin overrides ---
    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "Arp";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

    void initialise(const te::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void reset() override;

    void applyToBuffer(const te::PluginRenderContext&) override;

    void restorePluginStateFromValueTree(const juce::ValueTree&) override;

    // --- Parameter access for UI (CachedValues for persistence) ---
    juce::CachedValue<int> pattern;
    juce::CachedValue<int> rate;
    juce::CachedValue<int> octaveRange;
    juce::CachedValue<float> gate;
    juce::CachedValue<float> swing;
    juce::CachedValue<float> ramp;      // -1.0 to 1.0: bezier depth (perpendicular bow)
    juce::CachedValue<float> skew;      // -1.0 to 1.0: control-point position offset from centre
    juce::CachedValue<int> rampCycles;  // 1-8: curve repetitions within one arp cycle
    juce::CachedValue<bool> latch;
    juce::CachedValue<int> velocityMode;
    juce::CachedValue<int> fixedVelocity;
    juce::CachedValue<float> quantize;
    juce::CachedValue<int> quantizeSub;
    juce::CachedValue<bool> hardAngle;

    // --- Automatable parameters (for macro/mod linking) ---
    te::AutomatableParameter::Ptr patternParam, rateParam, octavesParam;
    te::AutomatableParameter::Ptr gateParam, swingParam;
    te::AutomatableParameter::Ptr rampParam, skewParam;
    te::AutomatableParameter::Ptr latchParam, velModeParam, fixedVelParam;

    /** Quadratic bezier timing curve. Control point at (skew, skew+depth) in graph space.
     *  skew=0.5, depth=0  → linear.
     *  depth > 0 → bowed above diagonal (front-loaded / log-like).
     *  depth < 0 → bowed below diagonal (back-loaded / exp-like).
     *  Moving skew away from 0.5 creates asymmetric curves. */
    static double applyRampCurve(double t, float depth, float skew, bool hardAngle = false);

    /** Current arp step and sequence length for UI (set on audio thread). */
    std::atomic<int> currentPlayStep_{-1};
    std::atomic<int> currentSeqLength_{0};

  private:
    // --- Audio-thread state ---
    static constexpr int MAX_HELD = 32;
    struct HeldNote {
        int noteNumber = -1;
        int velocity = 0;
        int order = 0;
    };
    std::array<HeldNote, MAX_HELD> heldNotes_{};
    int heldCount_ = 0;
    int nextOrder_ = 0;

    // Latch tracking
    int physicallyHeldCount_ = 0;
    bool latchedSetStale_ = false;

    // Pattern state
    int currentStep_ = 0;
    bool goingUp_ = true;
    double arpOriginBeat_ = -1.0;
    int lastPlayedNote_ = -1;
    int lastPlayedVelocity_ = 0;
    double lastNoteOffBeat_ = -1.0;

    // Transport
    bool wasPlaying_ = false;

    // Free-running clock for when transport is stopped
    double freeRunSamples_ = 0.0;

    // Random
    juce::Random arpRandom_;

    // Helper to sync CachedValue changes to AutomatableParams
    void syncParamFromProperty(const juce::Identifier& property);

    // Inner listener to forward ValueTree changes
    struct ParamSyncListener : public juce::ValueTree::Listener {
        ArpeggiatorPlugin& owner;
        explicit ParamSyncListener(ArpeggiatorPlugin& o) : owner(o) {}
        void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier& p) override {
            owner.syncParamFromProperty(p);
        }
    };
    ParamSyncListener paramSyncListener_{*this};

    // --- Helpers ---
    static double rateToBeats(Rate r);
    void addHeldNote(int noteNumber, int velocity);
    void removeHeldNote(int noteNumber);
    void clearHeldNotes();
    void sendAllNotesOff(te::MidiMessageArray& midi);
    void resetArpState();

    struct ExpandedSequence {
        std::array<HeldNote, MAX_HELD * 4> notes{};
        int length = 0;
    };
    ExpandedSequence buildSequence() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArpeggiatorPlugin)
};

}  // namespace magda::daw::audio
