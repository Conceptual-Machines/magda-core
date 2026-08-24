#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include "core/ParameterInfo.hpp"
#include "core/ParameterUtils.hpp"
#include "plugins/compiled/CompiledFaustInterface.hpp"

// The Faust dsp is forward-declared via its own base so this header does not
// pull in the Faust SDK; the .cpp owns it.
class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Shared base for MAGDA's compiled-Faust effects.
 *
 * The effect half of what MagdaCompiledPolyInstrument is for the instruments
 * (#2192). Every compiled effect was carrying its own copy of the same six
 * hundred lines -- an [idx:N] zone harvester, a normalized-parameter table, a
 * scratch-buffer dance around dsp::compute(), and a sanitising pass -- with the
 * device's actual identity in about forty of them. That duplication is why they
 * were still host-engine plugin subclasses: porting fifty independent copies of
 * the same plumbing is fifty jobs, and porting the plumbing once is one.
 *
 * What the base owns:
 *
 *  - **The slot table.** A concrete device returns slotInfos() once; the base
 *    builds a normalized CompiledParameterValue per slot, answers the whole
 *    ICompiledFaustPlugin surface from it, and derives each parameter's stable
 *    id as `slotIdPrefix() + name` (override slotId() when a device needs its
 *    own scheme).
 *
 *  - **The harvest.** Every dsp control is read for its [idx:N] slot, its
 *    `[style:menu{...}]` choices, its `[gate:N]` enable condition and its
 *    `[role:...]` tag before slotInfos() is asked for anything, so a device can
 *    build its slot table out of what the dsp declared rather than restating
 *    it. Gates are applied to the finished table by the base: a device never
 *    has to re-apply them after its own designated initializers wipe them.
 *
 *  - **The Faust engines.** engineCount() dsp instances, each harvested the
 *    same way, so a host slot finds its zone in whichever engines expose it.
 *    Every engine is written every block, not just the running one, so
 *    switching engines mid-project keeps the user's settings. A device with no
 *    Faust dsp at all (EQ, Utility) returns nothing from createEngineDsp() and
 *    overrides processAudio().
 *
 *  - **Project tempo.** A dsp control tagged `[role:projectTempo]` gets the
 *    host's BPM written into it every block, from the tempo map the process
 *    context carries. The tempo-synced effects used to reach into the current
 *    engine's tempo sequence for this, which is exactly the kind of reach that
 *    kept them tied to one engine.
 *
 *  - **The block.** Input copied out of the shared buffer (Faust forbids
 *    aliasing), compute(), then a finite/limit pass over what came back.
 *
 * A concrete device supplies its dsp factory, its slot table, its id and names,
 * and calls initEffect() from its constructor. Metering taps and any state that
 * is not a parameter hang off the beforeCompute()/afterCompute() hooks.
 */
class MagdaCompiledEffect : public CompiledFaustDevice {
  public:
    MagdaCompiledEffect();
    ~MagdaCompiledEffect() override;

    using HostSlotInfo = CompiledHostSlotInfo;

    void prepare(const DevicePrepareContext& context) override;
    void release() override;
    void reset() override;
    void process(DeviceProcessContext& context) override;

    DeviceProperties properties() const override;

    int hostSlotCountValue() const {
        return static_cast<int>(hostSlotInfo_.size());
    }

    DeviceParameterHandle getSlotParameter(int slotIndex) const;
    const HostSlotInfo& getSlotInfo(int slotIndex) const;
    float displayValueToNativeValue(int slotIndex, float displayValue) const;
    float nativeValueToDisplayValue(int slotIndex, float nativeValue) const;

    /// The slot's display-domain value as it is right now -- the value the last
    /// block wrote into the dsp. What a device UI reads.
    float slotDisplayValue(int slotIndex) const;

    double currentSampleRate() const {
        return sampleRate_.load(std::memory_order_relaxed);
    }

    /// Project tempo as of the last processed block. Tempo-synced devices draw
    /// their beat grid from this rather than re-querying the host.
    float currentBpm() const {
        return currentBpm_.load(std::memory_order_relaxed);
    }

    // ---- What the dsp declared, by [idx:N] --------------------------------
    // Valid from the first line of slotInfos() onward. A menu's labels are what
    // a device puts in a slot's choices; a menu's values are what the dsp wants
    // written into the zone, and the two are not always the same list.
    std::vector<juce::String> menuLabelsForIdx(int idx) const;
    std::vector<float> menuValuesForIdx(int idx) const;
    /// The Faust value the menu declared for @p choiceIndex, or 0 when there is
    /// no menu at that idx. A device UI that has to reconstruct what the dsp is
    /// doing (a delay's echo spacing from its Division choice) asks this.
    float menuValueForChoice(int idx, int choiceIndex) const;

    // ICompiledFaustPlugin
    int hostSlotCount() const override {
        return hostSlotCountValue();
    }
    const CompiledHostSlotInfo& hostSlotInfo(int slotIndex) const override {
        return getSlotInfo(slotIndex);
    }
    DeviceParameterHandle hostSlotParameter(int slotIndex) const override {
        return getSlotParameter(slotIndex);
    }
    juce::String hostSlotId(int slotIndex) const override;
    float displayToNormalized(int slotIndex, float displayValue) const override {
        return displayValueToNativeValue(slotIndex, displayValue);
    }
    float normalizedToDisplay(int slotIndex, float normalizedValue) const override {
        return nativeValueToDisplayValue(slotIndex, normalizedValue);
    }
    int activeEngine() const override;

  protected:
    // ---- Identity, supplied by the concrete device ------------------------
    /// The device's slots, in slot order. Called once, after the dsp has been
    /// harvested, so it may read menuLabelsForIdx() and friends.
    virtual std::vector<HostSlotInfo> slotInfos() const = 0;
    /// Parameter-id prefix, e.g. "magda_clipper_". Must be stable: it keys state.
    virtual const char* slotIdPrefix() const = 0;
    virtual juce::String devicePluginId() const = 0;
    virtual juce::String deviceName() const = 0;
    virtual juce::String deviceShortName() const {
        return deviceName();
    }
    /// Default is slotIdPrefix() + the slot name lowercased with spaces as
    /// underscores. Override where a device pins ids that do not follow it.
    virtual juce::String slotId(int slotIndex) const;

    // ---- The dsp ----------------------------------------------------------
    /// How many Faust engines this device switches between. One for most.
    virtual int engineCount() const {
        return 1;
    }
    /// Allocate engine @p engineIndex's dsp, or null for a device that has none
    /// and overrides processAudio() instead.
    virtual ::dsp* createEngineDsp(int engineIndex) const;
    /// The slot whose value picks the running engine. -1 for a single-engine
    /// device.
    virtual int engineSlot() const {
        return -1;
    }
    /// Which host slot a dsp control's [idx:N] drives, and which slot a
    /// `[gate:N]` refers to. The identity by default: a device pins its dsp
    /// indices to its slot indices. A device whose table interleaves
    /// wrapper-only controls (the Filter's Engine and Limit) says otherwise.
    /// Return -1 for a dsp index no host slot owns.
    virtual int slotForDspIdx(int idx) const {
        return idx;
    }

    // ---- Properties -------------------------------------------------------
    virtual bool wantsMidiInput() const {
        return false;
    }
    virtual bool wantsSidechain() const {
        return false;
    }
    /// Whether the host should keep pumping the device once dry input stops. A
    /// device with a tail in its delay lines says yes, or the trail is cut.
    virtual bool producesAudioWithoutInput() const {
        return false;
    }
    virtual double tailSeconds() const {
        return 0.0;
    }
    virtual double latencySeconds() const {
        return 0.0;
    }
    /// Fixed output width, or 0 to follow the input. See
    /// DeviceProperties::outputChannelCount.
    virtual int outputChannelCount() const {
        return 0;
    }
    /// Input channels the device reads, sidechain key included, or 0 to let the
    /// host decide. See DeviceProperties::inputChannelCount.
    virtual int inputChannelCount() const {
        return 0;
    }
    /// Whether a stopped->playing edge clears the dsp. An LFO-driven effect
    /// wants its phase back at zero every time the transport starts, so the
    /// modulation lines up with the song rather than with whenever the graph
    /// happened to be built.
    virtual bool resetsOnPlayStart() const {
        return false;
    }

    // ---- Per-device hooks -------------------------------------------------
    virtual void onPrepare(double /*sampleRate*/, int /*maximumBlockSize*/) {}
    virtual void onRelease() {}
    virtual void onReset() {}
    /// Zones the slot table does not cover, and any cross-slot correction the
    /// dsp needs. Called once per engine, after that engine's slot writes.
    virtual void writeExtraZones(int /*engineIndex*/) {}
    /// Audio thread, after the zone writes and before compute().
    virtual void beforeCompute(DeviceProcessContext& /*context*/, int /*engineIndex*/) {}
    /// Audio thread, after compute() and after the output has been sanitised.
    virtual void afterCompute(DeviceProcessContext& /*context*/, int /*engineIndex*/) {}
    /// The whole block. The default runs the active Faust engine; a device with
    /// no dsp overrides this and does its own work.
    virtual void processAudio(DeviceProcessContext& context);

    /// Concrete constructors call this once, after their hooks are valid.
    void initEffect();

    // ---- What a subclass may read ----------------------------------------
    float* zoneForIdx(int engineIndex, int idx) const;
    int engineInputCount(int engineIndex) const;
    int engineOutputCount(int engineIndex) const;
    magda::ParameterInfo infoForSlot(int slotIndex) const;
    /// The slot's conversion domain, cached when the table was built.
    ///
    /// What the audio thread converts through. ParameterInfo carries the slot's
    /// name, unit and choice list, and building one per slot per block copies
    /// that choice vector -- a heap allocation, in process(), on every discrete
    /// parameter of every device. ParameterDomain is the same conversion with
    /// none of the strings.
    const magda::ParameterUtils::ParameterDomain& domainForSlot(int slotIndex) const;
    /// Runs the given engine's dsp over the context's buffer, in place.
    void computeEngine(int engineIndex, DeviceProcessContext& context);

    /// Finite, and inside the same +/-16 ceiling every compiled device applies
    /// before handing a block back to the host.
    static float sanitise(float sample);

  private:
    /// What one dsp control declared about itself.
    struct HarvestedControl {
        int idx = -1;        ///< The dsp's own [idx:N].
        int slotIndex = -1;  ///< The host slot it drives, via slotForDspIdx().
        float* zone = nullptr;
        std::vector<float> menuValues;
        std::vector<juce::String> menuLabels;
        int gateSlotIndex = -1;
        bool gateNegated = false;
        bool isProjectTempo = false;
    };

    struct EngineState {
        std::unique_ptr<::dsp> instance;
        std::vector<HarvestedControl> harvested;
        /// Zone per host slot, null where this engine does not expose it. Bound
        /// once the slot table exists.
        std::vector<float*> zonesBySlot;
        float* projectTempoZone = nullptr;
        int numInputs = 0;
        int numOutputs = 0;
    };

    void createEngines(int sampleRate);
    void bindSlots();
    void applyHarvestedGates();
    void buildHostParameters();
    void writeZones(DeviceProcessContext& context);
    /// The first engine that declares anything about @p idx, which is where the
    /// menus and gates come from. Engines of one device agree about a shared
    /// control; where they do not, the first to declare it wins.
    const HarvestedControl* harvestedForIdx(int idx) const;

    std::vector<EngineState> engines_;

    std::vector<HostSlotInfo> hostSlotInfo_;
    std::vector<magda::ParameterUtils::ParameterDomain> slotDomains_;
    // Individually allocated so a DeviceParameterHandle handed out early stays
    // valid if the slot container is ever extended.
    std::vector<std::unique_ptr<CompiledParameterValue>> hostParams_;

    std::atomic<double> sampleRate_{44100.0};
    std::atomic<float> currentBpm_{120.0f};
    /// Audio thread only: the transport state the previous block ran under.
    bool wasPlaying_ = false;

    juce::AudioBuffer<float> scratchIn_;
    juce::AudioBuffer<float> scratchOut_;
    std::vector<float*> inPtrs_;
    std::vector<float*> outPtrs_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaCompiledEffect)
};

}  // namespace magda::daw::audio::compiled
