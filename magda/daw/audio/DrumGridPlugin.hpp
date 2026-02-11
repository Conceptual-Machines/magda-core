#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace magda::daw::audio {

namespace te = tracktion::engine;

//==============================================================================
/**
 * @brief Drum machine plugin with 64 pads, each hosting its own child plugin
 *
 * Each pad maps to a MIDI trigger note and can host a MagdaSamplerPlugin or
 * an external VST/AU. All pad outputs are mixed internally to a single stereo
 * output that flows to the track's mixer channel.
 */
class DrumGridPlugin : public te::Plugin {
  public:
    DrumGridPlugin(const te::PluginCreationInfo&);
    ~DrumGridPlugin() override;

    //==============================================================================
    static const char* getPluginName() {
        return "Drum Grid";
    }
    static const char* xmlTypeName;

    static constexpr int maxPads = 64;

    juce::String getName() const override {
        return getPluginName();
    }
    juce::String getPluginType() override {
        return xmlTypeName;
    }
    juce::String getShortName(int) override {
        return "DrumGrid";
    }
    juce::String getSelectableDescription() override {
        return getName();
    }

    //==============================================================================
    struct Pad {
        int triggerNote = -1;
        std::vector<te::Plugin::Ptr> plugins;
        juce::CachedValue<float> level;
        juce::CachedValue<float> pan;
        juce::CachedValue<bool> mute;
        juce::CachedValue<bool> solo;
    };

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
    double getTailLength() const override {
        return 1.0;
    }

    void restorePluginStateFromValueTree(const juce::ValueTree&) override;

    //==============================================================================
    // Pad management — instrument slot (replaces all plugins with a single instrument)
    void loadSampleToPad(int padIndex, const juce::File& file);
    void loadPluginToPad(int padIndex, const juce::PluginDescription& desc);
    void clearPad(int padIndex);
    const Pad& getPad(int padIndex) const;

    // FX chain management — add/remove/reorder effects after the instrument
    void addPluginToPad(int padIndex, const juce::PluginDescription& desc, int insertIndex = -1);
    void removePluginFromPad(int padIndex, int pluginIndex);
    void movePluginInPad(int padIndex, int fromIndex, int toIndex);
    int getPadPluginCount(int padIndex) const;
    te::Plugin* getPadPlugin(int padIndex, int pluginIndex) const;

  private:
    std::array<Pad, maxPads> pads_;
    juce::AudioBuffer<float> scratchBuffer_;
    te::MidiMessageArray padMidi_;
    double sampleRate_ = 44100.0;
    int blockSize_ = 512;

    static const juce::Identifier padTreeId;
    static const juce::Identifier triggerNoteId;
    static const juce::Identifier padLevelId;
    static const juce::Identifier padPanId;
    static const juce::Identifier padMuteId;
    static const juce::Identifier padSoloId;

    void initPadValueTrees();
    void initChildPlugin(te::Plugin& childPlugin);
    void deinitChildPlugin(te::Plugin& childPlugin);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumGridPlugin)
};

}  // namespace magda::daw::audio
