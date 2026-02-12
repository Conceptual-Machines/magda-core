#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <atomic>

namespace magda::daw::audio {

namespace te = tracktion::engine;

//==============================================================================
/**
 * @brief Drum machine plugin with chain-based model
 *
 * Each chain maps to a contiguous range of MIDI notes (pads) and hosts its own
 * plugin chain (instrument + FX). All chain outputs are mixed internally to a
 * single stereo output that flows to the track's mixer channel.
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
    static constexpr int baseNote = 36;  // Pad 0 = MIDI note 36 (C2)

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
    struct Chain {
        int index = 0;
        int lowNote = 60;   // bottom of MIDI note range (inclusive)
        int highNote = 60;  // top of MIDI note range (inclusive)
        int rootNote = 60;  // remap base: instrumentNote = rootNote + (incoming - lowNote)
        juce::String name;
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
    // Chain management
    int addChain(int lowNote, int highNote, int rootNote, const juce::String& name);
    void removeChain(int chainIndex);
    const std::vector<std::unique_ptr<Chain>>& getChains() const;
    const Chain* getChainForNote(int midiNote) const;
    const Chain* getChainByIndex(int chainIndex) const;
    Chain* getChainByIndexMutable(int chainIndex);

    // Convenience pad-level API (finds/creates single-note chain for padIndex)
    void loadSampleToPad(int padIndex, const juce::File& file);
    void loadPluginToPad(int padIndex, const juce::PluginDescription& desc);
    void clearPad(int padIndex);

    // FX chain management on chains
    void addPluginToChain(int chainIndex, const juce::PluginDescription& desc,
                          int insertIndex = -1);
    void removePluginFromChain(int chainIndex, int pluginIndex);
    void movePluginInChain(int chainIndex, int fromIndex, int toIndex);
    int getChainPluginCount(int chainIndex) const;
    te::Plugin* getChainPlugin(int chainIndex, int pluginIndex) const;

    // Pad trigger flags (set by audio thread, consumed by UI)
    bool consumePadTrigger(int padIndex);

    // Legacy pad-level FX API (delegates to chain-based methods)
    void addPluginToPad(int padIndex, const juce::PluginDescription& desc, int insertIndex = -1);
    void removePluginFromPad(int padIndex, int pluginIndex);
    void movePluginInPad(int padIndex, int fromIndex, int toIndex);
    int getPadPluginCount(int padIndex) const;
    te::Plugin* getPadPlugin(int padIndex, int pluginIndex) const;

  private:
    std::vector<std::unique_ptr<Chain>> chains_;
    int nextChainIndex_ = 0;
    juce::AudioBuffer<float> scratchBuffer_;
    te::MidiMessageArray padMidi_;
    double sampleRate_ = 44100.0;
    int blockSize_ = 512;
    bool wasPlaying_ = false;
    std::array<std::atomic<bool>, maxPads> padTriggered_{};

    static const juce::Identifier chainTreeId;
    static const juce::Identifier chainIndexId;
    static const juce::Identifier lowNoteId;
    static const juce::Identifier highNoteId;
    static const juce::Identifier rootNoteId;
    static const juce::Identifier chainNameId;
    static const juce::Identifier padLevelId;
    static const juce::Identifier padPanId;
    static const juce::Identifier padMuteId;
    static const juce::Identifier padSoloId;

    Chain* findChainForNote(int midiNote);
    Chain* findOrCreateChainForPad(int padIndex);
    void removeChainFromState(int chainIndex);
    juce::ValueTree findChainTree(int chainIndex) const;
    void initChildPlugin(te::Plugin& childPlugin);
    void deinitChildPlugin(te::Plugin& childPlugin);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumGridPlugin)
};

}  // namespace magda::daw::audio
