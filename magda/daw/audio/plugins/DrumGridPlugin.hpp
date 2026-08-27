#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "plugins/DeviceServices.hpp"

namespace magda {
struct ChainInfo;
struct DeviceInfo;
struct RackInfo;
}  // namespace magda

namespace magda::daw::audio {

namespace te = tracktion::engine;

//==============================================================================
/**
 * @brief Drum machine plugin with chain-based model
 *
 * Each chain maps to a contiguous range of MIDI notes (pads) and hosts its own
 * plugin chain (instrument + FX). All chain outputs are mixed internally to a
 * single stereo output that flows to the track's mixer channel.
 *
 * The chains are a mirror, not a model. Which pads exist and what sits on them
 * is `DeviceInfo::pads`, and `syncFromModel()` is the only thing that writes
 * them here, exactly as `RackSyncManager` fills a `te::RackType` from a
 * `RackInfo` (#2207). Nothing on this class edits a pad and nothing reads one
 * back out: an edit made here instead would be invisible to the plan, to undo
 * and to the project file until something happened to capture it.
 */
class DrumGridPlugin : public te::Plugin, private juce::Timer {
  public:
    explicit DrumGridPlugin(const te::PluginCreationInfo&);
    ~DrumGridPlugin() override;

    //==============================================================================
    static const char* getPluginName() {
        return "Drum Grid";
    }
    static const char* xmlTypeName;

    static constexpr int maxPads = 64;
    static constexpr int baseNote = 24;       // Pad 0 = MIDI note 24 (C0)
    static constexpr int maxBusOutputs = 32;  // TE RackType max is 64 audio pins = 32 stereo pairs

    /**
     * @brief Per-pad output gains for a given level and pan position.
     *
     * Linear pan law, matching Tracktion's default (PanLawLinear, see
     * getGainsFromVolumeFaderPositionAndPan) so a pad at centre pan is unity —
     * the same as a device sitting directly on a track. An equal-power law was
     * used here previously, which cost every pad 3 dB (cos(pi/4) = 0.707) at the
     * default centre pan and made Drum Grid devices quieter than the identical
     * device loaded standalone.
     *
     * Like Tracktion's, this is a pan rather than a balance law: at hard pan the
     * favoured channel reaches 2x. That is deliberate — a hard-panned pad and a
     * hard-panned track must agree.
     *
     * Defined here so the audio path and its tests share one definition.
     */
    static void computePadGains(float levelLinear, float panValue, float& leftGain,
                                float& rightGain) noexcept {
        const float pan = juce::jlimit(-1.0f, 1.0f, panValue);
        const float panGain = pan * levelLinear;
        leftGain = levelLinear - panGain;
        rightGain = levelLinear + panGain;
    }

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
        // Backing for the CachedValues below, and nothing else. Deliberately
        // NOT a child of the plugin's `state`: the pads are the model's, and a
        // copy of them under `state` would be captured into the device's saved
        // state and handed to Tracktion's graph builder as nested plugins
        // (#2207).
        juce::ValueTree tree;
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
    /// Builds the engine plugin a pad device names, or null when it cannot be
    /// built. Supplied by the host, which is the only side that knows how to
    /// resolve a `DeviceInfo` to a plugin (internal spec, compiled device,
    /// scanned external) and how to seat its saved patch.
    using PadPluginFactory = std::function<te::Plugin::Ptr(const magda::DeviceInfo&)>;

    /// Make the grid's chains and plugins match @p pads.
    ///
    /// The one way in. Chains and plugins are created, dropped and reordered to
    /// match the model, and every pad property is written from it; a plugin the
    /// model still names is kept, so a pad that did not change is not rebuilt
    /// and does not glitch. Cheap on every sync pass: with no structural change
    /// it only refreshes properties.
    void syncFromModel(const magda::RackInfo& pads, const PadPluginFactory& makePlugin);

    //==============================================================================
    // Chain reads. Everything here answers questions about the mirror the sync
    // built; none of it edits a pad.
    const std::vector<std::unique_ptr<Chain>>& getChains() const;
    const Chain* getChainForNote(int midiNote) const;
    const Chain* getChainByIndex(int chainIndex) const;
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

    // Per-plugin peak metering within a chain
    static constexpr int maxFxPerChain = 8;
    float getChainPluginGain(int chainIndex, int pluginIndex) const;
    std::pair<float, float> consumeChainPluginPeak(int chainIndex, int pluginIndex);

    // Mixer expand/collapse state (persisted in ValueTree)
    bool isMixerExpanded() const {
        return mixerExpanded_.get();
    }
    void setMixerExpanded(bool expanded) {
        mixerExpanded_ = expanded;
    }

    // Multi-out mode toggle (persisted in ValueTree). A pad's bus is
    // `ChainInfo::outputIndex` and is assigned in the model like every other
    // pad property; this is the grid's own switch, not a pad's (#2207).
    bool isMultiOutEnabled() const {
        return multiOutEnabled_.get();
    }

    int getNumOutputChannels() const {
        return maxBusOutputs * 2;
    }

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

    // The model DeviceId the pad plugin at this position carries. The one thing
    // the model and the mirror are guaranteed to agree on, so everything that
    // has to pair the two goes through it.
    int getPluginDeviceId(int chainIndex, int pluginIndex) const;

    // Pad-level reads (delegate to the chain covering the pad's note)
    int getPadPluginCount(int padIndex) const;
    te::Plugin* getPadPlugin(int padIndex, int pluginIndex) const;

  private:
    // Immutable, audio-thread-readable view of one chain. Holds owning Plugin::Ptr
    // copies so the graph stays alive for the duration of a process block even if
    // the message thread is concurrently rebuilding chains_. Note-range / remap
    // values are copied in (the audio thread must not read those mutable plain-int
    // Chain fields directly — they can be edited on the message thread). `chain` is
    // a raw, non-owning back-pointer used only for CachedValue control reads
    // (level/pan/mute/solo/bus, audio-thread-safe by design) and per-pad metering;
    // its lifetime is guaranteed by publishSnapshot() retiring the previous
    // snapshot before any Chain is freed.
    struct AudioChainEntry {
        Chain* chain = nullptr;
        int lowNote = 0;
        int highNote = 0;
        int rootNote = 0;
        std::vector<te::Plugin::Ptr> plugins;
        std::vector<float> gains;
    };
    using AudioSnapshot = std::vector<AudioChainEntry>;

    void processChain(const AudioChainEntry& entry, juce::AudioBuffer<float>& outputBuffer,
                      const te::MidiMessageArray& inputMidi, int numSamples, int numChannels,
                      const te::PluginRenderContext& rc);

    // A snapshot retired by a publish, kept alive (along with anything that must
    // outlive the audio thread's use of it) until the audio thread has released it.
    // Reaped on the message thread by drainRetired() — never freed under the audio
    // thread, and the publishing thread never blocks waiting for it.
    struct RetiredSnapshot {
        std::shared_ptr<const AudioSnapshot> guard;      // the previous published snapshot
        std::vector<te::Plugin::Ptr> reapPlugins;        // plugins removed by this publish
        std::vector<std::unique_ptr<Chain>> reapChains;  // chains removed by this publish
    };

    // Rebuild + atomically publish audioSnapshot_ from chains_ (RCU-style swap that
    // makes chain edits race-free without locking or glitching the audio thread).
    // Anything removed from chains_ by the caller is handed over via reapPlugins /
    // reapChains; it is deinitialised and freed later by drainRetired(), once the
    // audio thread has let go of the snapshot that still referenced it. The caller
    // must NOT free those objects itself — this method does not block.
    void publishSnapshot(std::vector<te::Plugin::Ptr> reapPlugins = {},
                         std::vector<std::unique_ptr<Chain>> reapChains = {});

    // Free retired snapshots (and their reap payloads) whose audio-thread references
    // have been released (use_count() == 1). Runs on the message thread, both
    // opportunistically from publishSnapshot() and from the reaper timer.
    void drainRetired();

    // juce::Timer — drives drainRetired() while anything is pending retirement.
    void timerCallback() override;

    // --- syncFromModel helpers. Nothing else may call these: together they are
    // the single write path from the model into the mirror.

    // The Chain carrying @p index, detached from chains_, or a fresh one, with
    // @p created saying which.
    std::unique_ptr<Chain> takeChain(int index, bool& created);

    // Write a pad's properties onto its Chain. Returns true when it moved
    // something the audio thread reads out of the published snapshot, which is
    // what decides whether the snapshot has to be republished.
    bool applyPadProperties(Chain& chain, const magda::ChainInfo& pad, bool created);

    // Make @p chain's plugins match the pad's devices, in model order. Anything
    // the model dropped is handed to @p reap for retirement rather than freed
    // here: the audio thread may still be on a snapshot that names it.
    void syncPadPlugins(Chain& chain, const magda::ChainInfo& pad,
                        const PadPluginFactory& makePlugin, std::vector<te::Plugin::Ptr>& reap);

    // Write each pad device's power onto the plugin built for it. Runs on every
    // pass: a bypass toggle changes no structure, so the rebuild above does not
    // see it.
    void applyPadPluginPower(Chain& chain, const magda::ChainInfo& pad);

    // What the mirror was last built from, so a sync pass that changes nothing
    // does nothing. Structure only: pad ids, their devices, and the order of
    // both. Properties are cheap to write and are refreshed every pass.
    static juce::String padStructureFingerprint(const magda::RackInfo& pads);
    juce::String padFingerprint_;

    // AutomatableParameters for per-pad level and pan (macro/mod targets)
    // Fixed indexing: padIndex * 2 = level, padIndex * 2 + 1 = pan
    std::array<te::AutomatableParameter::Ptr, maxPads> levelParams_;
    std::array<te::AutomatableParameter::Ptr, maxPads> panParams_;

    // Sync a chain's CachedValues → AutomatableParams
    void syncParamFromChain(const Chain& chain);

    std::vector<std::unique_ptr<Chain>> chains_;

    // Published, immutable snapshot the audio thread reads instead of chains_.
    // Written only by publishSnapshot() (message thread), loaded by
    // applyToBuffer() (audio thread). Accessed through the std::atomic_*(shared_ptr*)
    // free functions for a lock-free acquire/release handoff (this toolchain's
    // libc++ lacks the std::atomic<std::shared_ptr> specialisation).
    std::shared_ptr<const AudioSnapshot> audioSnapshot_;

    // Snapshots (and the plugins/chains they kept alive) awaiting message-thread
    // reaping once the audio thread releases them. Only touched on the message thread.
    std::vector<RetiredSnapshot> retired_;

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
    /// The model DeviceId of the pad device a plugin was built for, stamped by
    /// the sync. The mirror carries no other model state: everything else is
    /// read from the `RackInfo` each pass (#2207).
    static const juce::Identifier pluginDeviceIdProp;

    Chain* getChainByIndexMutable(int chainIndex);
    void notifyChainsChanged();

    juce::ListenerList<Listener> listeners_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumGridPlugin)
};

}  // namespace magda::daw::audio
