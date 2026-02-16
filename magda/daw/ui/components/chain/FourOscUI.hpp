#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/DeviceInfo.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Non-automatable plugin state for the 4OSC synth
 *
 * These are CachedValues that aren't exposed as AutomatableParameters,
 * so they must be read/written directly on the plugin object.
 */
struct FourOscPluginState {
    int oscWaveShape[4] = {0, 0, 0, 0};
    int oscVoices[4] = {1, 1, 1, 1};
    int filterType = 0;
    int filterSlope = 0;
    bool ampAnalog = false;
    int lfoWaveShape[2] = {0, 0};
    bool lfoSync[2] = {false, false};
    bool distortionOn = false;
    bool reverbOn = false;
    bool delayOn = false;
    bool chorusOn = false;
};

/**
 * @brief Custom tabbed UI for the 4OSC synthesizer
 *
 * 6 tabs: OSC, Filter, Amp, Mod Env, LFO, FX
 *
 * Automatable parameters use paramIndex-based callbacks through TrackManager.
 * Non-automatable CachedValues use a separate callback that writes directly
 * to the plugin's ValueTree state via AudioBridge.
 */
class FourOscUI : public juce::Component {
  public:
    FourOscUI();
    ~FourOscUI() override = default;

    /**
     * @brief Update all automatable parameter controls from device parameters
     */
    void updateFromParameters(const std::vector<magda::ParameterInfo>& params);

    /**
     * @brief Update non-automatable plugin state (waveform combos, toggles, etc.)
     */
    void updatePluginState(const FourOscPluginState& state);

    /**
     * @brief Callback for automatable parameter changes (paramIndex, value)
     */
    std::function<void(int paramIndex, float value)> onParameterChanged;

    /**
     * @brief Callback for non-automatable plugin state changes
     */
    std::function<void(const juce::String& propertyId, juce::var value)> onPluginStateChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    // =========================================================================
    // Tab content components
    // =========================================================================

    class OscTab : public juce::Component {
      public:
        OscTab(FourOscUI& owner);
        void resized() override;
        void updateFromParameters(const std::vector<magda::ParameterInfo>& params);
        void updatePluginState(const FourOscPluginState& state);

      private:
        FourOscUI& owner_;
        // Per-oscillator controls (4 oscillators)
        struct OscRow {
            juce::Label label;
            juce::ComboBox waveformCombo;
            TextSlider tuneSlider{TextSlider::Format::Decimal};
            TextSlider fineSlider{TextSlider::Format::Decimal};
            TextSlider levelSlider{TextSlider::Format::Decibels};
            TextSlider pulseWidthSlider{TextSlider::Format::Decimal};
            TextSlider detuneSlider{TextSlider::Format::Decimal};
            TextSlider spreadSlider{TextSlider::Format::Decimal};
            TextSlider panSlider{TextSlider::Format::Decimal};
            juce::ComboBox voicesCombo;
        };
        OscRow rows_[4];
        void setupLabel(juce::Label& label, const juce::String& text);
        // Column header labels
        juce::Label hdrWave_, hdrTune_, hdrFine_, hdrLevel_, hdrPW_, hdrDetune_, hdrSpread_,
            hdrPan_, hdrVoices_;
    };

    class FilterTab : public juce::Component {
      public:
        FilterTab(FourOscUI& owner);
        void resized() override;
        void updateFromParameters(const std::vector<magda::ParameterInfo>& params);
        void updatePluginState(const FourOscPluginState& state);

      private:
        FourOscUI& owner_;
        juce::ComboBox typeCombo_, slopeCombo_;
        TextSlider freqSlider_{TextSlider::Format::Decimal};
        TextSlider resonanceSlider_{TextSlider::Format::Decimal};
        TextSlider keySlider_{TextSlider::Format::Decimal};
        TextSlider velocitySlider_{TextSlider::Format::Decimal};
        TextSlider amountSlider_{TextSlider::Format::Decimal};
        // Filter envelope
        TextSlider attackSlider_{TextSlider::Format::Decimal};
        TextSlider decaySlider_{TextSlider::Format::Decimal};
        TextSlider sustainSlider_{TextSlider::Format::Decimal};
        TextSlider releaseSlider_{TextSlider::Format::Decimal};
        // Labels
        juce::Label typeLabel_, slopeLabel_, freqLabel_, resLabel_, keyLabel_, velLabel_,
            amountLabel_;
        juce::Label atkLabel_, decLabel_, susLabel_, relLabel_;
        void setupLabel(juce::Label& label, const juce::String& text);
    };

    class AmpTab : public juce::Component {
      public:
        AmpTab(FourOscUI& owner);
        void resized() override;
        void updateFromParameters(const std::vector<magda::ParameterInfo>& params);
        void updatePluginState(const FourOscPluginState& state);

      private:
        FourOscUI& owner_;
        TextSlider attackSlider_{TextSlider::Format::Decimal};
        TextSlider decaySlider_{TextSlider::Format::Decimal};
        TextSlider sustainSlider_{TextSlider::Format::Decimal};
        TextSlider releaseSlider_{TextSlider::Format::Decimal};
        TextSlider velocitySlider_{TextSlider::Format::Decimal};
        juce::ToggleButton analogButton_{"Analog"};
        juce::Label atkLabel_, decLabel_, susLabel_, relLabel_, velLabel_;
        void setupLabel(juce::Label& label, const juce::String& text);
    };

    class ModEnvTab : public juce::Component {
      public:
        ModEnvTab(FourOscUI& owner);
        void resized() override;
        void updateFromParameters(const std::vector<magda::ParameterInfo>& params);

      private:
        FourOscUI& owner_;
        struct EnvRow {
            juce::Label label;
            TextSlider attackSlider{TextSlider::Format::Decimal};
            TextSlider decaySlider{TextSlider::Format::Decimal};
            TextSlider sustainSlider{TextSlider::Format::Decimal};
            TextSlider releaseSlider{TextSlider::Format::Decimal};
        };
        EnvRow rows_[2];
        juce::Label hdrAtk_, hdrDec_, hdrSus_, hdrRel_;
        void setupLabel(juce::Label& label, const juce::String& text);
    };

    class LFOTab : public juce::Component {
      public:
        LFOTab(FourOscUI& owner);
        void resized() override;
        void updateFromParameters(const std::vector<magda::ParameterInfo>& params);
        void updatePluginState(const FourOscPluginState& state);

      private:
        FourOscUI& owner_;
        struct LFORow {
            juce::Label label;
            juce::ComboBox waveCombo;
            TextSlider rateSlider{TextSlider::Format::Decimal};
            TextSlider depthSlider{TextSlider::Format::Decimal};
            juce::ToggleButton syncButton{"Sync"};
        };
        LFORow rows_[2];
        juce::Label hdrWave_, hdrRate_, hdrDepth_, hdrSync_;
        void setupLabel(juce::Label& label, const juce::String& text);
    };

    class FXTab : public juce::Component {
      public:
        FXTab(FourOscUI& owner);
        void resized() override;
        void updateFromParameters(const std::vector<magda::ParameterInfo>& params);
        void updatePluginState(const FourOscPluginState& state);

      private:
        FourOscUI& owner_;
        // Distortion
        juce::ToggleButton distOnButton_{"Dist"};
        TextSlider distAmountSlider_{TextSlider::Format::Decimal};
        // Reverb
        juce::ToggleButton reverbOnButton_{"Reverb"};
        TextSlider reverbSizeSlider_{TextSlider::Format::Decimal};
        TextSlider reverbDampSlider_{TextSlider::Format::Decimal};
        TextSlider reverbWidthSlider_{TextSlider::Format::Decimal};
        TextSlider reverbMixSlider_{TextSlider::Format::Decimal};
        // Delay
        juce::ToggleButton delayOnButton_{"Delay"};
        TextSlider delayFeedbackSlider_{TextSlider::Format::Decibels};
        TextSlider delayCrossfeedSlider_{TextSlider::Format::Decibels};
        TextSlider delayMixSlider_{TextSlider::Format::Decimal};
        // Chorus
        juce::ToggleButton chorusOnButton_{"Chorus"};
        TextSlider chorusSpeedSlider_{TextSlider::Format::Decimal};
        TextSlider chorusDepthSlider_{TextSlider::Format::Decimal};
        TextSlider chorusWidthSlider_{TextSlider::Format::Decimal};
        TextSlider chorusMixSlider_{TextSlider::Format::Decimal};
        // Labels
        juce::Label distLabel_, revSizeLabel_, revDampLabel_, revWidthLabel_, revMixLabel_;
        juce::Label delFbLabel_, delXfLabel_, delMixLabel_;
        juce::Label chSpeedLabel_, chDepthLabel_, chWidthLabel_, chMixLabel_;
        void setupLabel(juce::Label& label, const juce::String& text);
    };

    // =========================================================================
    // Members
    // =========================================================================

    std::unique_ptr<juce::TabbedComponent> tabs_;
    std::unique_ptr<OscTab> oscTab_;
    std::unique_ptr<FilterTab> filterTab_;
    std::unique_ptr<AmpTab> ampTab_;
    std::unique_ptr<ModEnvTab> modEnvTab_;
    std::unique_ptr<LFOTab> lfoTab_;
    std::unique_ptr<FXTab> fxTab_;

    // Parameter index constants (based on FourOscPlugin::addParam order)
    // Osc 1-4: 7 params each (tune, fineTune, level, pulseWidth, detune, spread, pan)
    static constexpr int kOscParamsPerOsc = 7;
    static constexpr int kOscBase = 0;  // indices 0-27
    // LFO 1-2: 2 params each (rate, depth)
    static constexpr int kLfoParamsPerLfo = 2;
    static constexpr int kLfoBase = 28;  // indices 28-31
    // Mod Env 1-2: 4 params each (attack, decay, sustain, release)
    static constexpr int kModEnvParamsPerEnv = 4;
    static constexpr int kModEnvBase = 32;  // indices 32-39
    // Amp: 5 params (attack, decay, sustain, release, velocity)
    static constexpr int kAmpBase = 40;  // indices 40-44
    // Filter: 9 params (attack, decay, sustain, release, freq, resonance, amount, key, velocity)
    static constexpr int kFilterBase = 45;  // indices 45-53
    // Effects (added after modMatrix build)
    static constexpr int kDistBase = 54;    // index 54
    static constexpr int kReverbBase = 55;  // indices 55-58
    static constexpr int kDelayBase = 59;   // indices 59-61
    static constexpr int kChorusBase = 62;  // indices 62-65
    static constexpr int kGlobalBase = 66;  // indices 66-67

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FourOscUI)
};

}  // namespace magda::daw::ui
