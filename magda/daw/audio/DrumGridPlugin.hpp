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

    static constexpr int maxPads = 128;
    static constexpr int baseNote = 0;  // Pad 0 = MIDI note 0 (C-2)

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
    void setPadTriggered(int padIndex);
    bool consumePadTrigger(int padIndex);

    // Per-chain peak metering (set by audio thread, consumed by UI)
    struct ChainMeterData {
        std::atomic<float> peakL{0.0f};
        std::atomic<float> peakR{0.0f};
    };
    std::pair<float, float> consumeChainPeak(int chainIndex);

    // Mixer expand/collapse state (persisted in ValueTree)
    bool isMixerExpanded() const {
        return mixerExpanded_.get();
    }
    void setMixerExpanded(bool expanded) {
        mixerExpanded_ = expanded;
    }

    // Trigger graph rebuild when chain configuration changes
    void notifyGraphRebuildNeeded();

    // Legacy pad-level FX API (delegates to chain-based methods)
    void addPluginToPad(int padIndex, const juce::PluginDescription& desc, int insertIndex = -1);
    void removePluginFromPad(int padIndex, int pluginIndex);
    void movePluginInPad(int padIndex, int fromIndex, int toIndex);
    int getPadPluginCount(int padIndex) const;
    te::Plugin* getPadPlugin(int padIndex, int pluginIndex) const;

  private:
    std::vector<std::unique_ptr<Chain>> chains_;
    int nextChainIndex_ = 0;
    std::array<std::atomic<bool>, maxPads> padTriggered_{};
    std::array<ChainMeterData, maxPads> chainMeters_{};
    juce::CachedValue<bool> mixerExpanded_;

    // Audio processing state
    te::MidiMessageArray chainMidi_;
    double sampleRate_ = 44100.0;
    int blockSize_ = 512;

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
    static const juce::Identifier mixerExpandedId;

    Chain* findChainForNote(int midiNote);
    Chain* findOrCreateChainForPad(int padIndex);
    void removeChainFromState(int chainIndex);
    juce::ValueTree findChainTree(int chainIndex) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumGridPlugin)
};

}  // namespace magda::daw::audio
