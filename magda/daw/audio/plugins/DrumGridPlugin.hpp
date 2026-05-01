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
    static constexpr int baseNote = 24;       // Pad 0 = MIDI note 24 (C0)
    static constexpr int maxBusOutputs = 32;  // TE RackType max is 64 audio pins = 32 stereo pairs

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
        std::vector<float> pluginGains;  // per-plugin linear gain (parallel to plugins[])
        juce::CachedValue<float> level;
        juce::CachedValue<float> pan;
        juce::CachedValue<bool> mute;
        juce::CachedValue<bool> solo;
        juce::CachedValue<bool> bypassed;
        juce::CachedValue<int> busOutput;  // 0 = parent track main, 1+ = multi-out bus
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

    int getNumOutputChannelsGivenInputs(int /*numInputChannels*/) override {
        return getNumOutputChannels();
    }
    void getChannelNames(juce::StringArray* ins, juce::StringArray* outs) override {
        if (ins)
            ins->clear();
        if (outs) {
            outs->clear();
            for (int ch = 1; ch <= getNumOutputChannels(); ++ch)
                outs->add("Out " + juce::String(ch));
        }
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

    // Update note range for an existing chain
    void setChainNoteRange(int chainIndex, int lowNote, int highNote, int rootNote);

    // Convenience pad-level API (finds/creates single-note chain for padIndex)
    void loadSampleToPad(int padIndex, const juce::File& file);
    void loadPluginToPad(int padIndex, const juce::PluginDescription& desc);
    void clearPad(int padIndex);

    // Swap the chains of two pads (or move if only one has a chain)
    void swapPadChains(int padIndexA, int padIndexB);

    // FX chain management on chains
    void addPluginToChain(int chainIndex, const juce::PluginDescription& desc,
                          int insertIndex = -1);
    void addInternalPluginToChain(int chainIndex, const juce::String& pluginId,
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

    // Per-plugin gain and peak metering within a chain
    static constexpr int maxFxPerChain = 8;
    void setChainPluginGain(int chainIndex, int pluginIndex, float gainLinear);
    float getChainPluginGain(int chainIndex, int pluginIndex) const;
    std::pair<float, float> consumeChainPluginPeak(int chainIndex, int pluginIndex);

    // Mixer expand/collapse state (persisted in ValueTree)
    bool isMixerExpanded() const {
        return mixerExpanded_.get();
    }
    void setMixerExpanded(bool expanded) {
        mixerExpanded_ = expanded;
    }

    // Multi-out mode toggle (persisted in ValueTree)
    bool isMultiOutEnabled() const {
        return multiOutEnabled_.get();
    }
    void setMultiOutEnabled(bool enabled);

    // Multi-out bus management
    int getNumOutputChannels() const {
        return maxBusOutputs * 2;
    }
    void setChainBusOutput(int chainIndex, int busIndex);
    void assignBusOutputs();
    void fullReassignBusOutputs();
    int getNextFreeBus() const;
    int getActiveBusCount() const;

    // Trigger graph rebuild when chain configuration changes
    void notifyGraphRebuildNeeded();

    // Listener for chain add/remove events (used by MixerView)
    struct Listener {
        virtual ~Listener() = default;
        virtual void drumGridChainsChanged(DrumGridPlugin* plugin) = 0;
    };
    void addListener(Listener* l) {
        listeners_.add(l);
    }
    void removeListener(Listener* l) {
        listeners_.remove(l);
    }

    // Per-chain-plugin DeviceId for macro/mod linking
    int getPluginDeviceId(int chainIndex, int pluginIndex) const;

    // Legacy pad-level FX API (delegates to chain-based methods)
    void addPluginToPad(int padIndex, const juce::PluginDescription& desc, int insertIndex = -1);
    void removePluginFromPad(int padIndex, int pluginIndex);
    void movePluginInPad(int padIndex, int fromIndex, int toIndex);
    int getPadPluginCount(int padIndex) const;
    te::Plugin* getPadPlugin(int padIndex, int pluginIndex) const;

  private:
    void processChain(Chain& chain, juce::AudioBuffer<float>& outputBuffer,
                      const te::MidiMessageArray& inputMidi, int numSamples, int numChannels,
                      const te::PluginRenderContext& rc);

    // AutomatableParameters for per-pad level and pan (macro/mod targets)
    // Fixed indexing: padIndex * 2 = level, padIndex * 2 + 1 = pan
    std::array<te::AutomatableParameter::Ptr, maxPads> levelParams_;
    std::array<te::AutomatableParameter::Ptr, maxPads> panParams_;

    // Sync existing chain CachedValues → AutomatableParams
    void syncParamFromChain(int chainIndex);

    // ValueTree::Listener — sync CachedValue changes to AutomatableParams
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property) override;

    std::vector<std::unique_ptr<Chain>> chains_;
    int nextChainIndex_ = 0;
    std::array<std::atomic<bool>, maxPads> padTriggered_{};
    std::array<ChainMeterData, maxPads> chainMeters_{};
    std::array<std::array<ChainMeterData, maxFxPerChain>, maxPads> pluginMeters_{};
    juce::CachedValue<bool> mixerExpanded_;
    juce::CachedValue<bool> multiOutEnabled_;

    // Audio processing state
    te::MidiMessageArray chainMidi_;
    juce::AudioBuffer<float> scratchBuffer_;  // pre-allocated stereo scratch for processChain
    double sampleRate_ = 44100.0;
    int blockSize_ = 512;

    // Internal helper: pad-array index for a chain (lowNote - baseNote), or -1 if out of range
    int padIndexFor(const Chain& chain) const {
        int p = chain.lowNote - baseNote;
        return (p >= 0 && p < maxPads) ? p : -1;
    }

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
    static const juce::Identifier padBypassedId;
    static const juce::Identifier busOutputId;
    static const juce::Identifier mixerExpandedId;
    static const juce::Identifier multiOutEnabledId;
    static const juce::Identifier pluginDeviceIdProp;

    Chain* findChainForNote(int midiNote);
    Chain* findOrCreateChainForPad(int padIndex);
    void removeChainFromState(int chainIndex);
    juce::ValueTree findChainTree(int chainIndex) const;
    void notifyChainsChanged();

    juce::ListenerList<Listener> listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumGridPlugin)
};

}  // namespace magda::daw::audio
