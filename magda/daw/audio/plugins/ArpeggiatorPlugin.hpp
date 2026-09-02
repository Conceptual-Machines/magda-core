#pragma once

#include <array>
#include <atomic>

#include "core/ParameterUtils.hpp"
#include "plugins/MidiMagdaDevice.hpp"

namespace magda::daw::audio {

/**
 * @brief MIDI arpeggiator device that transforms held notes into rhythmic patterns.
 *
 * Placed on a track's FX chain before a synth. Captures incoming MIDI note-on/off
 * events, clears the MIDI buffer, and outputs arpeggiated notes synced to the
 * block's tempo map. All processing happens on the audio thread.
 *
 * A MagdaDevice since #2299: one scheduler hosted by whichever engine is
 * running it. The slot ids, order and display ranges are the ones the retired
 * host-native plugin registered. The ramp-cycles / quantize / hard-angle
 * settings were never parameters - they persist as device state under the
 * retired property names and the faceplate writes them directly.
 */
class ArpeggiatorPlugin : public MidiMagdaDevice {
  public:
    ArpeggiatorPlugin();
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

    /// FROZEN slot order - saved links address these by index.
    enum ParamIndex {
        kPattern = 0,
        kRate,
        kOctaves,
        kGate,
        kSwing,
        kRamp,  // -1..1: bezier depth (perpendicular bow)
        kSkew,  // -1..1: control-point position offset from centre
        kLatch,
        kVelMode,
        kFixedVel,
        kNumParams
    };

    DeviceProperties properties() const override {
        return {
            .pluginId = xmlTypeName,
            .name = getPluginName(),
            .shortName = "Arp",
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

    // --- Non-parameter settings (persisted device state, written by the UI) ---
    // Atomics: the faceplate writes on the message thread while process() reads.
    std::atomic<int> rampCycles{1};      // 1-8: curve repetitions within one arp cycle
    std::atomic<float> quantize{0.0f};   // 0..1: pull warped steps toward a regular grid
    std::atomic<int> quantizeSub{16};    // grid subdivisions for quantize
    std::atomic<bool> hardAngle{false};  // sharp-cornered ramp curve

    /** Quadratic bezier timing curve. Control point at (skew, skew+depth) in graph space.
     *  skew=0.5, depth=0  → linear.
     *  depth > 0 → bowed above diagonal (front-loaded / log-like).
     *  depth < 0 → bowed below diagonal (back-loaded / exp-like).
     *  Moving skew away from 0.5 creates asymmetric curves. */
    static double applyRampCurve(double t, float depth, float skew, bool hardAngle = false);

    /** Current arp step and sequence length for UI (set on audio thread). */
    std::atomic<int> currentPlayStep_{-1};
    std::atomic<int> currentSeqLength_{0};

    /** Modulated Time Bend position for the UI curve display, refreshed every
     *  block. The parameter mirror itself is audio-thread-owned, so the 30 Hz
     *  UI timer reads these instead of racing it. */
    std::atomic<float> displayedRamp_{0.0f};
    std::atomic<float> displayedSkew_{0.0f};

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

    /// The parameter's display-domain value, converted through the cached
    /// domain rather than a freshly built ParameterInfo: this runs per block on
    /// the audio thread, and a ParameterInfo carries strings that allocate.
    float displayValue(int index) const;
    int displayIndex(int index) const;

    std::array<float, kNumParams> values_{};
    std::array<ParameterUtils::ParameterDomain, kNumParams> domains_{};

    // --- Helpers ---
    static double rateToBeats(Rate r);
    void addHeldNote(int noteNumber, int velocity);
    void removeHeldNote(int noteNumber);
    void clearHeldNotes();
    void sendAllNotesOff(DeviceMidiBuffer& midi);
    void resetArpState();

    struct ExpandedSequence {
        std::array<HeldNote, MAX_HELD * 4> notes{};
        int length = 0;
    };
    ExpandedSequence buildSequence() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArpeggiatorPlugin)
};

}  // namespace magda::daw::audio
