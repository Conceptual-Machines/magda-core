#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <magda/sdk/device/Device.hpp>
#include <magda/sdk/device/ParameterDescriptor.hpp>
#include <memory>
#include <string>
#include <vector>

// Forward-declared through its own base so this header does not pull in the Faust SDK.
class dsp;

namespace magda::devices::faust {

/// One host slot of a compiled effect, as the parameter list and the manifest show it.
struct SlotInfo {
    std::string name;
    std::string unit;
    sdk::ParameterScale scale = sdk::ParameterScale::Linear;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float defaultValue = 0.0f;
    float scaleAnchor = std::numeric_limits<float>::quiet_NaN();
    /// Discrete slots.
    std::vector<std::string> choices;
    /// From `[gate:N]` / `[gate:!N]` on the dsp control; -1 is always enabled.
    int gateSlotIndex = -1;
    bool gateNegated = false;
};

/**
 * @brief Base of MAGDA's compiled-Faust effects (#2192), JUCE-free so they also build for the
 *        SDK hosts (#2940).
 *
 * It harvests every engine's dsp for [idx:N] slots, menus, gates and the project-tempo role, owns
 * the normalized slot values, writes every engine's zones each block, and runs the active engine
 * over the block. A concrete device supplies its dsp factory, slot table, id and names, and calls
 * initEffect() from its constructor.
 */
class CompiledEffect : public sdk::Device {
  public:
    CompiledEffect();
    ~CompiledEffect() override;

    void setHost(sdk::DeviceHost* host) override {
        host_ = host;
    }
    void prepare(const sdk::PrepareContext& context) override;
    void release() override;
    void reset() override;
    void process(sdk::ProcessContext& context) override;

    sdk::DeviceProperties properties() const override;
    int latencySamples() const override;
    std::int64_t tailSamples() const override;

    int parameterCount() const override {
        return hostSlotCount();
    }
    sdk::ParameterDescriptor parameterDescriptor(int slotIndex) const override;
    float parameterValue(int slotIndex) const override;
    void setParameterValue(int slotIndex, float normalized) override;

    int hostSlotCount() const {
        return static_cast<int>(slotInfo_.size());
    }
    /// An empty slot for an index outside the table.
    const SlotInfo& slotInfo(int slotIndex) const;
    /// The slot's stable parameter id.
    std::string hostSlotId(int slotIndex) const {
        return slotId(slotIndex);
    }

    /// The slot's normalized value cell, valid for the device's lifetime; null outside the table.
    std::atomic<float>* slotValue(int slotIndex) const;

    float displayToNormalized(int slotIndex, float displayValue) const;
    float normalizedToDisplay(int slotIndex, float normalizedValue) const;
    /// The slot's display-domain value now: what the last block wrote into the dsp.
    float slotDisplayValue(int slotIndex) const;

    double currentSampleRate() const {
        return sampleRate_.load(std::memory_order_relaxed);
    }
    /// Project tempo as of the last processed block.
    float currentBpm() const {
        return currentBpm_.load(std::memory_order_relaxed);
    }

    // What the dsp declared at [idx:N], valid from slotInfos() on. A menu's labels are a slot's
    // choices; its values are what the zone takes, which is not always the choice position.
    std::vector<std::string> menuLabelsForIdx(int idx) const;
    std::vector<float> menuValuesForIdx(int idx) const;
    /// The Faust value the menu declared for @p choiceIndex, or 0 with no menu at @p idx.
    float menuValueForChoice(int idx, int choiceIndex) const;

    virtual int activeEngine() const;
    /// The slot whose choices follow the active engine, or -1.
    virtual int engineAwareModeSlot() const {
        return -1;
    }
    /// The engine-aware slot's choices for the active engine.
    virtual std::vector<std::string> engineModeChoices() const {
        return {};
    }
    virtual bool isSlotHiddenForActiveEngine(int) const {
        return false;
    }

  protected:
    /// The slots, in slot order. Called once, after the harvest, so it may read the menus.
    virtual std::vector<SlotInfo> slotInfos() const = 0;
    /// Parameter-id prefix, e.g. "magda_clipper_". Stable: it keys state.
    virtual const char* slotIdPrefix() const = 0;
    virtual std::string devicePluginId() const = 0;
    virtual std::string deviceName() const = 0;
    virtual std::string deviceShortName() const {
        return deviceName();
    }
    /// slotIdPrefix() plus the lowercased slot name, spaces as underscores, by default.
    virtual std::string slotId(int slotIndex) const;

    virtual int engineCount() const {
        return 1;
    }
    /// Engine @p engineIndex's dsp, or null for a device that overrides processAudio() instead.
    virtual ::dsp* createEngineDsp(int engineIndex) const;
    /// The slot whose value picks the running engine; -1 for one engine.
    virtual int engineSlot() const {
        return -1;
    }
    /// The host slot a dsp [idx:N] (or a [gate:N]) refers to; -1 for none.
    virtual int slotForDspIdx(int idx) const {
        return idx;
    }

    virtual bool wantsMidiInput() const {
        return false;
    }
    /// The key the dsp reads as the inputs after its own (#2329); copied in by the base.
    virtual sdk::SidechainPort sidechainPort() const {
        return {};
    }
    /// True for a device with a tail the host must keep pumping once input stops.
    virtual bool producesAudioWithoutInput() const {
        return false;
    }
    virtual double tailSeconds() const {
        return 0.0;
    }
    virtual double latencySeconds() const {
        return 0.0;
    }
    /// See DeviceProperties::outputChannelCount.
    virtual int outputChannelCount() const {
        return 0;
    }
    /// See DeviceProperties::inputChannelCount.
    virtual int inputChannelCount() const {
        return 0;
    }
    /// Clear the dsp on a stopped-to-playing edge, so LFO phase lines up with the song.
    virtual bool resetsOnPlayStart() const {
        return false;
    }

    virtual void onPrepare(double /*sampleRate*/, int /*maximumBlockSize*/) {}
    virtual void onRelease() {}
    virtual void onReset() {}
    /// Zones the slot table does not cover. Once per engine, after its slot writes.
    virtual void writeExtraZones(int /*engineIndex*/) {}
    /// Audio thread, after the zone writes and before compute().
    virtual void beforeCompute(sdk::ProcessContext& /*context*/, int /*engineIndex*/) {}
    /// Audio thread, after compute() and the sanitising pass.
    virtual void afterCompute(sdk::ProcessContext& /*context*/, int /*engineIndex*/) {}
    /// The whole block: the active engine by default.
    virtual void processAudio(sdk::ProcessContext& context);

    /// Concrete constructors call this once, after their hooks are valid.
    void initEffect();

    /// The host this device reports to, or null when detached. Control thread.
    sdk::DeviceHost* host() const {
        return host_;
    }

    float* zoneForIdx(int engineIndex, int idx) const;
    int engineInputCount(int engineIndex) const;
    int engineOutputCount(int engineIndex) const;
    /// Cached when the table is built: the audio thread converts through it without allocating.
    const sdk::ParameterDomain& domainForSlot(int slotIndex) const;
    void computeEngine(int engineIndex, sdk::ProcessContext& context);

    /// Finite and inside +/-16. Inline: out of line it costs a call per sample and blocks
    /// vectorising the loop around it.
    static float sanitise(float sample) {
        return std::isfinite(sample) ? std::clamp(sample, -16.0f, 16.0f) : 0.0f;
    }

  private:
    struct HarvestedControl {
        int idx = -1;
        int slotIndex = -1;
        float* zone = nullptr;
        std::vector<float> menuValues;
        std::vector<std::string> menuLabels;
        int gateSlotIndex = -1;
        bool gateNegated = false;
        bool isProjectTempo = false;
    };

    struct EngineState {
        std::unique_ptr<::dsp> instance;
        std::vector<HarvestedControl> harvested;
        /// Zone per host slot, null where this engine does not expose it.
        std::vector<float*> zonesBySlot;
        float* projectTempoZone = nullptr;
        int numInputs = 0;
        int numOutputs = 0;
    };

    void createEngines(int sampleRate);
    void bindSlots();
    void applyHarvestedGates();
    void buildHostParameters();
    void writeZones(sdk::ProcessContext& context);
    /// The first engine that declares @p idx: engines of one device agree on a shared control.
    const HarvestedControl* harvestedForIdx(int idx) const;

    std::vector<EngineState> engines_;
    std::vector<SlotInfo> slotInfo_;
    std::vector<sdk::ParameterDomain> slotDomains_;
    std::unique_ptr<std::atomic<float>[]> slotValues_;

    sdk::DeviceHost* host_ = nullptr;
    std::atomic<double> sampleRate_{44100.0};
    std::atomic<float> currentBpm_{120.0f};
    /// Audio thread: the transport state the previous block ran under.
    bool wasPlaying_ = false;

    /// Planar, channel c at c * scratchFrames_. Sized in prepare; grown only if a block outruns it.
    std::vector<float> scratchIn_;
    std::vector<float> scratchOut_;
    int scratchFrames_ = 0;
    void ensureScratch(int inputs, int outputs, int frames);
    std::vector<float*> inPtrs_;
    std::vector<float*> outPtrs_;
};

}  // namespace magda::devices::faust
