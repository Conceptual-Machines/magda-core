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

    void restorePluginStateFromValueTree(const juce::ValueTree& v) override;

    // --- Parameters (CachedValues for persistence) ---
    juce::CachedValue<int> numSteps;
    juce::CachedValue<int> rate;       // StepClock::Rate enum
    juce::CachedValue<int> direction;  // StepClock::Direction enum
    juce::CachedValue<float> swing;
    juce::CachedValue<float> glideTime;  // 0-1 normalized
    juce::CachedValue<int> accentVelocity;
    juce::CachedValue<int> normalVelocity;

    // --- Automatable parameters (for macro/mod linking) ---
    te::AutomatableParameter::Ptr rateParam, directionParam;
    te::AutomatableParameter::Ptr swingParam, glideTimeParam;
    te::AutomatableParameter::Ptr accentVelParam, normalVelParam;

    // --- Step access (message thread) ---
    Step getStep(int index) const;
    void setStepNote(int index, int noteNumber);
    void setStepOctaveShift(int index, int shift);
    void setStepGate(int index, bool gate);
    void setStepAccent(int index, bool accent);
    void setStepGlide(int index, bool glide);
    void clearStep(int index);

    /** Current playback step index for UI highlight (-1 if not playing). */
    std::atomic<int> currentPlayStep_{-1};

  private:
    // Step clock (handles timing, transport, swing, direction)
    StepClock stepClock_;

    // --- Step state (persisted in ValueTree) ---
    std::array<Step, MAX_STEPS> steps_{};

    // --- Audio-thread state ---
    int lastPlayedNote_ = -1;
    double lastNoteOffBeat_ = -1.0;

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
