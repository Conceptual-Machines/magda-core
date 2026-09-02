#pragma once

#include <array>
#include <atomic>

#include "core/ParameterUtils.hpp"
#include "plugins/MidiMagdaDevice.hpp"
#include "sequencer/PolyStepSequencer.hpp"

namespace magda::daw::audio {

/**
 * @brief Polyphonic step sequencer MIDI device.
 *
 * Like StepSequencerPlugin but each step holds up to 8 notes: steps carry
 * gate, tie, probability and a step velocity, and a note may override that
 * velocity. The playing itself is sequencer::PolyStepSequencer; this class is
 * the device shell around it.
 *
 * A MagdaDevice since #2299, and the pattern belongs to the model rather than
 * to the device (#2313) - see StepSequencerPlugin for both.
 */
class PolyStepSequencerPlugin : public MidiMagdaDevice {
  public:
    PolyStepSequencerPlugin();
    ~PolyStepSequencerPlugin() override;

    static const char* getPluginName() {
        return "Poly Sequencer";
    }
    static const char* xmlTypeName;

    // --- Per-step data (the sequencing core's, so a pattern is one type) ---
    using Note = sequencer::PolyNote;
    using Step = sequencer::PolyStep;
    static constexpr int MAX_STEPS = sequencer::kMaxSteps;
    static constexpr int MAX_NOTES_PER_STEP = sequencer::kMaxNotesPerStep;

    /// FROZEN slot order - saved links address these by index.
    enum ParamIndex {
        kRate = 0,
        kDirection,
        kSwing,
        kGateLength,
        kRamp,  // -1..1: bezier timing depth
        kSkew,  // -1..1: control-point position offset from centre
        kNumParams
    };

    DeviceProperties properties() const override {
        return {
            .pluginId = xmlTypeName,
            .name = getPluginName(),
            .shortName = "PSeq",
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

    void flushState(juce::ValueTree& state) override;
    void restoreState(const juce::ValueTree& state) override;

    // ValueTree property ids for the non-slot settings below, in the retired
    // host-native plugin's spellings so saved projects keep them. Public
    // because the faceplate's settings edits travel as model document patches
    // in this vocabulary (#2317).
    struct SettingIDs {
        static const juce::Identifier numSteps;
        static const juce::Identifier midiThru;
        static const juce::Identifier rampCycles;
        static const juce::Identifier hardAngle;
        static const juce::Identifier quantize;
        static const juce::Identifier quantizeSub;
        static const juce::Identifier viewMode;
    };

    // --- Non-parameter settings (persisted device state) ---
    // Atomics: restoreState() writes on the message thread while process() reads.
    std::atomic<bool> midiThru{true};
    std::atomic<int> rampCycles{1};
    std::atomic<bool> hardAngle{false};
    std::atomic<float> quantize{0.0f};
    std::atomic<int> quantizeSub{16};

    /** Pattern-view mode for the faceplate ("keys" or "drum"). Persisted with
     *  the project; the engine does not act on it. Message thread only. */
    juce::String viewMode() const {
        return viewMode_;
    }

    /// The pattern the model last published. Message thread only - the
    /// faceplate reads it to draw, and edits it through the model.
    sequencer::PolyPattern pattern() const;

    /** Current playback step index for UI highlight (-1 if not playing). */
    std::atomic<int> currentPlayStep_{-1};

    // --- Step recording: play notes to fill steps. A chord (notes held
    // together) lands on one step; the step advances when the chord releases.
    // The device records what it heard and the faceplate commits it to the
    // model, which is where a pattern lives.

    /** One note the step recorder captured. */
    struct RecordedStep {
        int stepIndex = 0;
        int noteNumber = 60;
    };

    bool isStepRecording() const {
        return stepRecording_.load(std::memory_order_relaxed);
    }
    void setStepRecording(bool enabled);

    /** Take the oldest recorded note, if any. Message thread. */
    bool popRecordedStep(RecordedStep& step);

    std::atomic<int> stepRecordPosition_{0};  // next step to write (UI highlight)

  private:
    // The sequencing engine: clock, chord voice and the tie/probability rules.
    sequencer::PolyStepSequencer sequencer_;

    /// The published pattern, double-buffered (see StepSequencerPlugin).
    std::array<sequencer::PolyPattern, 2> patternSlots_{};
    std::atomic<int> livePattern_{0};

    juce::String viewMode_{"keys"};

    // --- Audio-thread state ---
    bool needsAllNotesOff_ = false;

    static constexpr int kMaxThruMessages = 64;
    std::array<juce::MidiMessage, kMaxThruMessages> thruMessages_{};
    std::array<std::uint32_t, kMaxThruMessages> thruSources_{};

    // --- Step recording state ---
    std::atomic<bool> stepRecording_{false};
    int recordHeldCount_ = 0;  // notes held during recording (audio thread)
    static constexpr int kRecordQueueSize = 64;
    std::array<RecordedStep, kRecordQueueSize> recordQueue_{};
    // Unsigned so the ring's indices wrap defined rather than overflowing.
    std::atomic<unsigned> recordWriteIndex_{0};
    std::atomic<unsigned> recordReadIndex_{0};

    /// See StepSequencerPlugin::displayValue - a per-block conversion that must
    /// not build a ParameterInfo.
    float displayValue(int index) const;
    int displayIndex(int index) const;

    std::array<float, kNumParams> values_{};
    std::array<ParameterUtils::ParameterDomain, kNumParams> domains_{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PolyStepSequencerPlugin)
};

}  // namespace magda::daw::audio
