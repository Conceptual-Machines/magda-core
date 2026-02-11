#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace magda::daw::audio {

namespace te = tracktion::engine;

//==============================================================================
/**
 * @brief Holds loaded sample data for the sampler
 */
struct SamplerSound : public juce::SynthesiserSound {
    juce::AudioBuffer<float> audioData;
    double sourceSampleRate = 44100.0;
    int rootNote = 60;

    bool appliesToNote(int) override {
        return true;
    }
    bool appliesToChannel(int) override {
        return true;
    }

    bool hasData() const {
        return audioData.getNumSamples() > 0;
    }
};

//==============================================================================
/**
 * @brief Voice for sample playback with ADSR envelope and pitch control
 */
class SamplerVoice : public juce::SynthesiserVoice {
  public:
    SamplerVoice();

    void setADSR(float attack, float decay, float sustain, float release);
    void setPitchOffset(float semitones, float cents);

    bool canPlaySound(juce::SynthesiserSound* sound) override;
    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*,
                   int currentPitchWheelPosition) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample,
                         int numSamples) override;

    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}

  private:
    juce::ADSR adsr;
    juce::ADSR::Parameters adsrParams;
    double pitchRatio = 1.0;
    double sourceSamplePosition = 0.0;
    float velocityGain = 0.0f;
    float pitchSemitones = 0.0f;
    float fineCents = 0.0f;
};

//==============================================================================
/**
 * @brief Sample-based instrument plugin with ADSR, pitch/fine, and level controls
 */
class MagdaSamplerPlugin : public te::Plugin {
  public:
    MagdaSamplerPlugin(const te::PluginCreationInfo&);
    ~MagdaSamplerPlugin() override;

    //==============================================================================
    static const char* getPluginName() {
        return "Magda Sampler";
    }
    static const char* xmlTypeName;

    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "Sampler";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

    //==============================================================================
    void initialise(const te::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void reset() override;

    void applyToBuffer(const te::PluginRenderContext&) override;

    //==============================================================================
    bool takesMidiInput() override {
        return true;
    }
    bool takesAudioInput() override {
        return false;
    }
    bool isSynth() override {
        return true;
    }
    bool producesAudioWhenNoAudioInput() override {
        return true;
    }
    double getTailLength() const override;

    void restorePluginStateFromValueTree(const juce::ValueTree&) override;

    //==============================================================================
    // Sample loading
    void loadSample(const juce::File& file);
    juce::File getSampleFile() const;
    const juce::AudioBuffer<float>* getWaveform() const;
    double getSampleLengthSeconds() const;
    int getRootNote() const;
    void setRootNote(int note);

    //==============================================================================
    // Automatable parameters
    juce::CachedValue<float> attackValue, decayValue, sustainValue, releaseValue;
    juce::CachedValue<float> pitchValue, fineValue, levelValue;

    te::AutomatableParameter::Ptr attackParam, decayParam, sustainParam, releaseParam;
    te::AutomatableParameter::Ptr pitchParam, fineParam, levelParam;

    // Non-parameter state
    juce::CachedValue<juce::String> samplePathValue;
    juce::CachedValue<int> rootNoteValue;

  private:
    //==============================================================================
    juce::Synthesiser synthesiser;
    SamplerSound* currentSound = nullptr;  // owned by synthesiser
    double sampleRate = 44100.0;
    int numVoices = 8;

    void updateVoiceParameters();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaSamplerPlugin)
};

}  // namespace magda::daw::audio
