#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/DeviceInfo.hpp"
#include "ui/components/common/IconSelector.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"

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
    int voiceMode = 2;      // 0=Mono, 1=Legato, 2=Poly
    int globalVoices = 32;  // Max polyphony
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

    void updateFromParameters(const std::vector<magda::ParameterInfo>& params);
    void updatePluginState(const FourOscPluginState& state);

    std::function<void(int paramIndex, float value)> onParameterChanged;
    std::function<void(const juce::String& propertyId, juce::var value)> onPluginStateChanged;

    // Get all linkable sliders for mod/macro wiring (in parameter-index order)
    std::vector<LinkableTextSlider*> getLinkableSliders();

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    // =========================================================================
    // Tab content components
    // =========================================================================

    class OscTab : public juce::Component {
        friend class FourOscUI;

      public:
        OscTab(FourOscUI& owner);
        void resized() override;
        void updateFromParameters(const std::vector<magda::ParameterInfo>& params);
        void updatePluginState(const FourOscPluginState& state);

      private:
        FourOscUI& owner_;
        struct OscRow {
            juce::Label label;
            IconSelector waveSelector;
            LinkableTextSlider tuneSlider{TextSlider::Format::Decimal};
            LinkableTextSlider fineSlider{TextSlider::Format::Decimal};
            LinkableTextSlider levelSlider{TextSlider::Format::Decibels};
            LinkableTextSlider pulseWidthSlider{TextSlider::Format::Decimal};
            LinkableTextSlider detuneSlider{TextSlider::Format::Decimal};
            LinkableTextSlider spreadSlider{TextSlider::Format::Decimal};
            LinkableTextSlider panSlider{TextSlider::Format::Decimal};
            LinkableTextSlider voicesSlider{TextSlider::Format::Decimal};
        };
        OscRow rows_[4];
        // Global controls
        IconSelector voiceModeSelector_;
        LinkableTextSlider globalVoicesSlider_{TextSlider::Format::Decimal};
        LinkableTextSlider legatoSlider_{TextSlider::Format::Decimal};
        LinkableTextSlider masterLevelSlider_{TextSlider::Format::Decibels};
        juce::Label modeLabel_, gVoicesLabel_, legatoLabel_, masterLabel_;
        void setupLabel(juce::Label& label, const juce::String& text);
        static void setupWaveSelector(IconSelector& selector);
        juce::Label hdrWave_, hdrTune_, hdrFine_, hdrLevel_, hdrPW_, hdrDetune_, hdrSpread_,
            hdrPan_, hdrVoices_;
    };

    class FilterTab : public juce::Component {
        friend class FourOscUI;

      public:
        FilterTab(FourOscUI& owner);
        void resized() override;
        void paint(juce::Graphics& g) override;
        void updateFromParameters(const std::vector<magda::ParameterInfo>& params);
        void updatePluginState(const FourOscPluginState& state);

      private:
        FourOscUI& owner_;
        IconSelector typeSelector_;
        IconSelector slopeSelector_;
        LinkableTextSlider freqSlider_{TextSlider::Format::Decimal};
        LinkableTextSlider resonanceSlider_{TextSlider::Format::Decimal};
        LinkableTextSlider keySlider_{TextSlider::Format::Decimal};
        LinkableTextSlider velocitySlider_{TextSlider::Format::Decimal};
        LinkableTextSlider amountSlider_{TextSlider::Format::Decimal};
        // Filter envelope
        LinkableTextSlider attackSlider_{TextSlider::Format::Decimal};
        LinkableTextSlider decaySlider_{TextSlider::Format::Decimal};
        LinkableTextSlider sustainSlider_{TextSlider::Format::Decimal};
        LinkableTextSlider releaseSlider_{TextSlider::Format::Decimal};
        // Labels
        juce::Label typeLabel_, slopeLabel_, freqLabel_, resLabel_, keyLabel_, velLabel_,
            amountLabel_;
        juce::Label atkLabel_, decLabel_, susLabel_, relLabel_;
        void setupLabel(juce::Label& label, const juce::String& text);
    };

    class AmpTab : public juce::Component {
        friend class FourOscUI;

      public:
        AmpTab(FourOscUI& owner);
        void resized() override;
        void paint(juce::Graphics& g) override;
        void updateFromParameters(const std::vector<magda::ParameterInfo>& params);
        void updatePluginState(const FourOscPluginState& state);

      private:
        FourOscUI& owner_;
        LinkableTextSlider attackSlider_{TextSlider::Format::Decimal};
        LinkableTextSlider decaySlider_{TextSlider::Format::Decimal};
        LinkableTextSlider sustainSlider_{TextSlider::Format::Decimal};
        LinkableTextSlider releaseSlider_{TextSlider::Format::Decimal};
        LinkableTextSlider velocitySlider_{TextSlider::Format::Decimal};
        juce::ToggleButton analogButton_{"Analog"};
        juce::Label atkLabel_, decLabel_, susLabel_, relLabel_, velLabel_;
        void setupLabel(juce::Label& label, const juce::String& text);
    };

    class ModEnvTab : public juce::Component {
        friend class FourOscUI;

      public:
        ModEnvTab(FourOscUI& owner);
        void resized() override;
        void paint(juce::Graphics& g) override;
        void updateFromParameters(const std::vector<magda::ParameterInfo>& params);

      private:
        FourOscUI& owner_;
        struct EnvRow {
            juce::Label label;
            LinkableTextSlider attackSlider{TextSlider::Format::Decimal};
            LinkableTextSlider decaySlider{TextSlider::Format::Decimal};
            LinkableTextSlider sustainSlider{TextSlider::Format::Decimal};
            LinkableTextSlider releaseSlider{TextSlider::Format::Decimal};
        };
        EnvRow rows_[2];
        juce::Label hdrAtk_, hdrDec_, hdrSus_, hdrRel_;
        void setupLabel(juce::Label& label, const juce::String& text);
    };

    class LFOTab : public juce::Component {
        friend class FourOscUI;

      public:
        LFOTab(FourOscUI& owner);
        void resized() override;
        void updateFromParameters(const std::vector<magda::ParameterInfo>& params);
        void updatePluginState(const FourOscPluginState& state);

      private:
        FourOscUI& owner_;
        struct LFORow {
            juce::Label label;
            IconSelector waveSelector;
            LinkableTextSlider rateSlider{TextSlider::Format::Decimal};
            LinkableTextSlider depthSlider{TextSlider::Format::Decimal};
            juce::ToggleButton syncButton{"Sync"};
        };
        LFORow rows_[2];
        static void setupWaveSelector(IconSelector& selector);
        juce::Label hdrWave_, hdrRate_, hdrDepth_, hdrSync_;
        void setupLabel(juce::Label& label, const juce::String& text);
    };

    class FXTab : public juce::Component {
        friend class FourOscUI;

      public:
        FXTab(FourOscUI& owner);
        void resized() override;
        void updateFromParameters(const std::vector<magda::ParameterInfo>& params);
        void updatePluginState(const FourOscPluginState& state);

      private:
        FourOscUI& owner_;
        // Distortion
        juce::ToggleButton distOnButton_{"Dist"};
        LinkableTextSlider distAmountSlider_{TextSlider::Format::Decimal};
        // Reverb
        juce::ToggleButton reverbOnButton_{"Reverb"};
        LinkableTextSlider reverbSizeSlider_{TextSlider::Format::Decimal};
        LinkableTextSlider reverbDampSlider_{TextSlider::Format::Decimal};
        LinkableTextSlider reverbWidthSlider_{TextSlider::Format::Decimal};
        LinkableTextSlider reverbMixSlider_{TextSlider::Format::Decimal};
        // Delay
        juce::ToggleButton delayOnButton_{"Delay"};
        LinkableTextSlider delayFeedbackSlider_{TextSlider::Format::Decibels};
        LinkableTextSlider delayCrossfeedSlider_{TextSlider::Format::Decibels};
        LinkableTextSlider delayMixSlider_{TextSlider::Format::Decimal};
        // Chorus
        juce::ToggleButton chorusOnButton_{"Chorus"};
        LinkableTextSlider chorusSpeedSlider_{TextSlider::Format::Decimal};
        LinkableTextSlider chorusDepthSlider_{TextSlider::Format::Decimal};
        LinkableTextSlider chorusWidthSlider_{TextSlider::Format::Decimal};
        LinkableTextSlider chorusMixSlider_{TextSlider::Format::Decimal};
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
    static constexpr int kOscParamsPerOsc = 7;
    static constexpr int kOscBase = 0;
    static constexpr int kLfoParamsPerLfo = 2;
    static constexpr int kLfoBase = 28;
    static constexpr int kModEnvParamsPerEnv = 4;
    static constexpr int kModEnvBase = 32;
    static constexpr int kAmpBase = 40;
    static constexpr int kFilterBase = 45;
    static constexpr int kDistBase = 54;
    static constexpr int kReverbBase = 55;
    static constexpr int kDelayBase = 59;
    static constexpr int kChorusBase = 62;
    static constexpr int kGlobalBase = 66;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FourOscUI)
};

}  // namespace magda::daw::ui
