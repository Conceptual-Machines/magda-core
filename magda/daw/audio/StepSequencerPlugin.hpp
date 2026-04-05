#pragma once

#include <array>

#include "MidiDevicePlugin.hpp"
#include "StepClock.hpp"

namespace magda::daw::audio {

/**
 * @brief 303-style monophonic step sequencer MIDI device.
 *
 * Generates a looping sequence of MIDI notes from an internal step pattern.
 * Each step has pitch, accent, glide, gate (on/off), and octave shift.
 * Placed on a track's FX chain before a synth.
 *
 * Uses StepClock for tempo-synced step timing (transport or free-running).
 */
class StepSequencerPlugin : public MidiDevicePlugin {
  public:
    StepSequencerPlugin(const te::PluginCreationInfo& info);
    ~StepSequencerPlugin() override;

    static const char* getPluginName() {
        return "Step Sequencer";
    }
    static const char* xmlTypeName;

    // --- Per-step data ---
    struct Step {
        int noteNumber = 60;  // MIDI note (C4 default)
        int octaveShift = 0;  // -2 to +2
        bool gate = true;     // true = active, false = rest
        bool accent = false;
        bool glide = false;  // Portamento to next step
        bool tie = false;    // Extend previous note (no retrigger)
    };

    static constexpr int MAX_STEPS = 32;

    // --- te::Plugin overrides ---
    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "Seq";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

    void initialise(const te::PluginInitialisationInfo& info) override;
    void deinitialise() override;
    void reset() override;

    void applyToBuffer(const te::PluginRenderContext& fc) override;

    void flushPluginStateToValueTree() override;
    void restorePluginStateFromValueTree(const juce::ValueTree& v) override;

    // --- Parameters (CachedValues for persistence) ---
    juce::CachedValue<int> numSteps;
    juce::CachedValue<int> rate;       // StepClock::Rate enum
    juce::CachedValue<int> direction;  // StepClock::Direction enum
    juce::CachedValue<float> swing;
    juce::CachedValue<float> gateLength;  // 0-1 normalized (0.1 = staccato, 1.0 = legato)
    juce::CachedValue<int> accentVelocity;
    juce::CachedValue<int> normalVelocity;
    juce::CachedValue<float> ramp;       // -1.0 to 1.0: bezier timing depth
    juce::CachedValue<float> skew;       // -1.0 to 1.0: bezier control point offset
    juce::CachedValue<int> rampCycles;   // 1-8: curve repetitions within one pattern cycle
    juce::CachedValue<bool> hardAngle;   // true = piecewise linear, false = smooth bezier
    juce::CachedValue<float> quantize;   // 0.0-1.0: adaptive quantize strength
    juce::CachedValue<int> quantizeSub;  // quantize grid subdivisions (16, 32, 48... 256)

    // --- Automatable parameters (for macro/mod linking) ---
    te::AutomatableParameter::Ptr rateParam, directionParam;
    te::AutomatableParameter::Ptr swingParam, gateLengthParam;
    te::AutomatableParameter::Ptr accentVelParam, normalVelParam;
    te::AutomatableParameter::Ptr rampParam, skewParam;

    // --- Step access (message thread) ---
    Step getStep(int index) const;
    void setStepNote(int index, int noteNumber);
    void setStepOctaveShift(int index, int shift);
    void setStepGate(int index, bool gate);
    void setStepAccent(int index, bool accent);
    void setStepGlide(int index, bool glide);
    void setStepTie(int index, bool tie);
    void clearStep(int index);

    /** Randomize all active steps with random notes, gates, accents, and glides. */
    void randomizePattern();

    /** Bulk-set the pattern from an external source (e.g. AI generation).
     *  If cueOnBar is true and transport is playing, the new pattern is queued
     *  and swapped in at the next cycle boundary (bar start). */
    void setPattern(const std::vector<Step>& steps, bool cueOnBar = false);

    /** Current playback step index for UI highlight (-1 if not playing). */
    std::atomic<int> currentPlayStep_{-1};

    /** MIDI thru: pass incoming MIDI to downstream plugins. */
    juce::CachedValue<bool> midiThru;

    /** Step record mode: incoming notes are recorded to steps sequentially. */
    std::atomic<bool> stepRecording_{false};
    bool isStepRecording() const {
        return stepRecording_.load(std::memory_order_relaxed);
    }
    void setStepRecording(bool enabled);

    /** Current step record position (for UI). */
    std::atomic<int> stepRecordPosition_{0};

  private:
    // Step clock (handles timing, transport, swing, direction)
    StepClock stepClock_;

    // --- Step state (persisted in ValueTree) ---
    std::array<Step, MAX_STEPS> steps_{};

    // --- Audio-thread state ---
    int lastPlayedNote_ = -1;
    int noteOffCountdown_ = 0;       // Samples remaining until note-off (0 = no pending)
    int silentBlockCount_ = 0;       // Blocks with no step events (for safety note-off)
    bool needsAllNotesOff_ = false;  // Send all-notes-off on next applyToBuffer

    // Previous timing params — detect structural changes that require clock reset
    int prevRate_ = -1;
    int prevCycles_ = 1;
    bool prevHardAngle_ = false;

    // Pending pattern — queued by setPattern(cueOnBar=true), swapped in at cycle boundary
    std::array<Step, MAX_STEPS> pendingSteps_{};
    int pendingNumSteps_ = 0;  // 0 = no pending pattern

    /** Kill any playing note immediately (audio thread). */
    void killNote(te::MidiMessageArray& midi, double time);

    // --- Helpers ---
    /** Resolve the effective note number for a step (noteNumber + octaveShift * 12). */
    static int resolveNote(const Step& step);

    // Save/load steps to/from ValueTree
    void saveStepsToState();
    void loadStepsFromState();

    // Sync CachedValue changes to AutomatableParams
    void syncParamFromProperty(const juce::Identifier& property);

    struct ParamSyncListener : public juce::ValueTree::Listener {
        StepSequencerPlugin& owner;
        explicit ParamSyncListener(StepSequencerPlugin& o) : owner(o) {}
        void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier& p) override {
            owner.syncParamFromProperty(p);
        }
    };
    ParamSyncListener paramSyncListener_{*this};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSequencerPlugin)
};

}  // namespace magda::daw::audio
