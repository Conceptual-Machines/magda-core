#include "plugins/compiled/MagdaCompiledEffect.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "core/ParameterUtils.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "plugins/FaustMetadataParser.hpp"

namespace magda::daw::audio::compiled {

namespace {

/// The one [idx:N] harvester the compiled effects used to each carry a copy of.
///
/// Records every numeric control by the slot index its label pins it to, plus
/// its menu, its gate condition and its role tag: everything the base needs to
/// bind a host slot to a zone without knowing which device it is looking at.
class EffectZoneHarvester : public ::UI {
  public:
    struct Control {
        int idx = -1;
        FAUSTFLOAT* zone = nullptr;
        std::vector<std::pair<float, juce::String>> menuChoices;
        int gateSlotIndex = -1;
        bool gateNegated = false;
        bool isProjectTempo = false;
    };

    std::vector<Control> controls;

    void openTabBox(const char*) override {}
    void openHorizontalBox(const char*) override {}
    void openVerticalBox(const char*) override {}
    void closeBox() override {}

    void addButton(const char* label, FAUSTFLOAT* zone) override {
        emit(label, zone);
    }
    void addCheckButton(const char* label, FAUSTFLOAT* zone) override {
        emit(label, zone);
    }
    void addVerticalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT,
                           FAUSTFLOAT) override {
        emit(label, zone);
    }
    void addHorizontalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT, FAUSTFLOAT,
                             FAUSTFLOAT, FAUSTFLOAT) override {
        emit(label, zone);
    }
    void addNumEntry(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT,
                     FAUSTFLOAT) override {
        emit(label, zone);
    }
    void addHorizontalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addVerticalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addSoundfile(const char*, const char*, Soundfile**) override {}

    void declare(FAUSTFLOAT* zone, const char* key, const char* value) override {
        if (zone == nullptr)
            return;
        const auto k = juce::String::fromUTF8(key != nullptr ? key : "").toLowerCase();
        const auto v = juce::String::fromUTF8(value != nullptr ? value : "");
        applyFaustAnnotation(k, v, pendingByZone_[zone]);
    }

  private:
    void emit(const char* rawLabel, FAUSTFLOAT* zone) {
        if (zone == nullptr)
            return;

        const auto parsed =
            parseFaustLabel(juce::String::fromUTF8(rawLabel != nullptr ? rawLabel : ""));
        ControlMetadata merged = parsed.metadata;
        if (auto it = pendingByZone_.find(zone); it != pendingByZone_.end()) {
            mergeFaustMetadata(merged, it->second);
            pendingByZone_.erase(it);
        }

        Control control;
        control.idx = merged.slotIndex;
        control.zone = zone;
        control.menuChoices = merged.menuChoices;
        control.gateSlotIndex = merged.gateSlotIndex;
        control.gateNegated = merged.gateNegated;
        control.isProjectTempo = merged.role == FaustControlRole::ProjectTempo;
        controls.push_back(std::move(control));
    }

    std::map<FAUSTFLOAT*, ControlMetadata> pendingByZone_;
};

}  // namespace

// ===========================================================================

MagdaCompiledEffect::MagdaCompiledEffect() = default;
MagdaCompiledEffect::~MagdaCompiledEffect() = default;

::dsp* MagdaCompiledEffect::createEngineDsp(int) const {
    return nullptr;
}

void MagdaCompiledEffect::initEffect() {
    // The harvest first, because a device builds its slot table out of what the
    // dsp declared: the tempo-synced effects take their Division choices
    // straight from the menu rather than restating the list in C++.
    //
    // The real sample rate arrives with prepare(); this provisional one makes
    // the device answerable from the moment it is constructed, which is what
    // the app does with one before it ever reaches a graph.
    constexpr int kProvisionalSampleRate = 44100;
    createEngines(kProvisionalSampleRate);

    hostSlotInfo_ = slotInfos();
    applyHarvestedGates();
    buildHostParameters();
    bindSlots();
}

void MagdaCompiledEffect::createEngines(int sampleRate) {
    sampleRate_.store(static_cast<double>(sampleRate), std::memory_order_relaxed);

    const int count = std::max(0, engineCount());
    engines_.clear();
    engines_.resize(static_cast<size_t>(count));

    for (int engineIndex = 0; engineIndex < count; ++engineIndex) {
        auto& engine = engines_[static_cast<size_t>(engineIndex)];
        engine.instance.reset(createEngineDsp(engineIndex));
        if (engine.instance == nullptr)
            continue;

        engine.instance->init(sampleRate);
        engine.numInputs = engine.instance->getNumInputs();
        engine.numOutputs = engine.instance->getNumOutputs();

        EffectZoneHarvester harvester;
        engine.instance->buildUserInterface(&harvester);

        for (const auto& control : harvester.controls) {
            if (control.isProjectTempo)
                engine.projectTempoZone = control.zone;
            if (control.idx < 0)
                continue;

            HarvestedControl harvested;
            harvested.idx = control.idx;
            harvested.zone = control.zone;
            harvested.gateSlotIndex = control.gateSlotIndex;
            harvested.gateNegated = control.gateNegated;
            harvested.isProjectTempo = control.isProjectTempo;

            auto sorted = control.menuChoices;
            std::sort(sorted.begin(), sorted.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
            harvested.menuValues.reserve(sorted.size());
            harvested.menuLabels.reserve(sorted.size());
            for (const auto& choice : sorted) {
                harvested.menuValues.push_back(choice.first);
                harvested.menuLabels.push_back(choice.second);
            }

            engine.harvested.push_back(std::move(harvested));
        }
    }
}

void MagdaCompiledEffect::bindSlots() {
    for (auto& engine : engines_) {
        engine.zonesBySlot.assign(static_cast<size_t>(hostSlotCountValue()), nullptr);
        for (const auto& harvested : engine.harvested)
            if (harvested.idx < hostSlotCountValue())
                engine.zonesBySlot[static_cast<size_t>(harvested.idx)] = harvested.zone;
    }
}

void MagdaCompiledEffect::applyHarvestedGates() {
    // After slotInfos(), never before: a device builds its table with
    // designated initializers, which zero every field it does not name, so a
    // gate written in first would be wiped by the device's own table.
    for (int slotIndex = 0; slotIndex < hostSlotCountValue(); ++slotIndex) {
        const auto* harvested = harvestedForIdx(slotIndex);
        if (harvested == nullptr || harvested->gateSlotIndex < 0)
            continue;

        auto& slot = hostSlotInfo_[static_cast<size_t>(slotIndex)];
        slot.gateSlotIndex = harvested->gateSlotIndex;
        slot.gateNegated = harvested->gateNegated;
    }
}

const MagdaCompiledEffect::HarvestedControl* MagdaCompiledEffect::harvestedForIdx(int idx) const {
    for (const auto& engine : engines_)
        for (const auto& harvested : engine.harvested)
            if (harvested.idx == idx)
                return &harvested;
    return nullptr;
}

std::vector<juce::String> MagdaCompiledEffect::menuLabelsForIdx(int idx) const {
    const auto* harvested = harvestedForIdx(idx);
    return harvested != nullptr ? harvested->menuLabels : std::vector<juce::String>{};
}

std::vector<float> MagdaCompiledEffect::menuValuesForIdx(int idx) const {
    const auto* harvested = harvestedForIdx(idx);
    return harvested != nullptr ? harvested->menuValues : std::vector<float>{};
}

float MagdaCompiledEffect::menuValueForChoice(int idx, int choiceIndex) const {
    const auto* harvested = harvestedForIdx(idx);
    if (harvested == nullptr || harvested->menuValues.empty())
        return 0.0f;

    const int safeIndex =
        juce::jlimit(0, static_cast<int>(harvested->menuValues.size()) - 1, choiceIndex);
    return harvested->menuValues[static_cast<size_t>(safeIndex)];
}

void MagdaCompiledEffect::buildHostParameters() {
    hostParams_.clear();
    hostParams_.reserve(hostSlotInfo_.size());

    for (int slotIndex = 0; slotIndex < hostSlotCountValue(); ++slotIndex) {
        const auto& slot = hostSlotInfo_[static_cast<size_t>(slotIndex)];
        hostParams_.push_back(std::make_unique<CompiledParameterValue>(
            magda::ParameterUtils::realToNormalized(slot.defaultValue, infoForSlot(slotIndex))));
    }
}

magda::ParameterInfo MagdaCompiledEffect::infoForSlot(int slotIndex) const {
    magda::ParameterInfo info;
    if (slotIndex < 0 || slotIndex >= hostSlotCountValue())
        return info;

    const auto& slot = hostSlotInfo_[static_cast<size_t>(slotIndex)];
    info.minValue = slot.minValue;
    info.maxValue = slot.maxValue;
    info.defaultValue = slot.defaultValue;
    info.unit = slot.unit;
    info.scale = slot.scale;
    if (std::isfinite(slot.scaleAnchor))
        info.scaleAnchor = slot.scaleAnchor;
    info.choices = slot.choices;
    return info;
}

DeviceParameterHandle MagdaCompiledEffect::getSlotParameter(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= static_cast<int>(hostParams_.size()))
        return {};
    return hostParams_[static_cast<size_t>(slotIndex)]->handle();
}

const MagdaCompiledEffect::HostSlotInfo& MagdaCompiledEffect::getSlotInfo(int slotIndex) const {
    static const HostSlotInfo kEmpty;
    if (slotIndex < 0 || slotIndex >= hostSlotCountValue())
        return kEmpty;
    return hostSlotInfo_[static_cast<size_t>(slotIndex)];
}

juce::String MagdaCompiledEffect::slotId(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= hostSlotCountValue())
        return {};
    return juce::String(slotIdPrefix()) +
           hostSlotInfo_[static_cast<size_t>(slotIndex)].name.toLowerCase().replace(" ", "_");
}

juce::String MagdaCompiledEffect::hostSlotId(int slotIndex) const {
    return slotId(slotIndex);
}

float MagdaCompiledEffect::displayValueToNativeValue(int slotIndex, float displayValue) const {
    if (slotIndex < 0 || slotIndex >= hostSlotCountValue())
        return displayValue;
    return magda::ParameterUtils::realToNormalized(displayValue, infoForSlot(slotIndex));
}

float MagdaCompiledEffect::nativeValueToDisplayValue(int slotIndex, float nativeValue) const {
    if (slotIndex < 0 || slotIndex >= hostSlotCountValue())
        return nativeValue;
    return magda::ParameterUtils::normalizedToReal(nativeValue, infoForSlot(slotIndex));
}

float MagdaCompiledEffect::slotDisplayValue(int slotIndex) const {
    return nativeValueToDisplayValue(slotIndex, getSlotParameter(slotIndex).currentValue());
}

int MagdaCompiledEffect::activeEngine() const {
    const int slot = engineSlot();
    if (slot < 0 || engines_.empty())
        return 0;
    return juce::jlimit(0, static_cast<int>(engines_.size()) - 1,
                        static_cast<int>(std::lround(slotDisplayValue(slot))));
}

float* MagdaCompiledEffect::zoneForIdx(int engineIndex, int idx) const {
    if (engineIndex < 0 || engineIndex >= static_cast<int>(engines_.size()))
        return nullptr;

    for (const auto& harvested : engines_[static_cast<size_t>(engineIndex)].harvested)
        if (harvested.idx == idx)
            return harvested.zone;
    return nullptr;
}

int MagdaCompiledEffect::engineInputCount(int engineIndex) const {
    if (engineIndex < 0 || engineIndex >= static_cast<int>(engines_.size()))
        return 0;
    return engines_[static_cast<size_t>(engineIndex)].numInputs;
}

int MagdaCompiledEffect::engineOutputCount(int engineIndex) const {
    if (engineIndex < 0 || engineIndex >= static_cast<int>(engines_.size()))
        return 0;
    return engines_[static_cast<size_t>(engineIndex)].numOutputs;
}

DeviceProperties MagdaCompiledEffect::properties() const {
    return {
        .pluginId = devicePluginId(),
        .name = deviceName(),
        .shortName = deviceShortName(),
        .takesMidiInput = wantsMidiInput(),
        .takesAudioInput = true,
        .isSynth = false,
        .producesAudioWithoutInput = false,
        .canSidechain = wantsSidechain(),
        .latencySeconds = latencySeconds(),
        .tailLengthSeconds = tailSeconds(),
        .outputChannelCount = outputChannelCount(),
    };
}

void MagdaCompiledEffect::prepare(const DevicePrepareContext& context) {
    createEngines(static_cast<int>(context.sampleRate));
    bindSlots();

    int maxInputs = 0;
    int maxOutputs = 0;
    for (const auto& engine : engines_) {
        maxInputs = std::max(maxInputs, engine.numInputs);
        maxOutputs = std::max(maxOutputs, engine.numOutputs);
    }

    scratchIn_.setSize(maxInputs, context.maximumBlockSize, false, true, true);
    scratchOut_.setSize(maxOutputs, context.maximumBlockSize, false, true, true);
    inPtrs_.assign(static_cast<size_t>(maxInputs), nullptr);
    outPtrs_.assign(static_cast<size_t>(maxOutputs), nullptr);

    onPrepare(context.sampleRate, context.maximumBlockSize);
}

void MagdaCompiledEffect::release() {
    onRelease();
    scratchIn_.setSize(0, 0);
    scratchOut_.setSize(0, 0);
    inPtrs_.clear();
    outPtrs_.clear();
}

void MagdaCompiledEffect::reset() {
    for (auto& engine : engines_)
        if (engine.instance)
            engine.instance->instanceClear();
    onReset();
}

float MagdaCompiledEffect::sanitise(float sample) {
    return std::isfinite(sample) ? juce::jlimit(-16.0f, 16.0f, sample) : 0.0f;
}

void MagdaCompiledEffect::writeZones(DeviceProcessContext& context) {
    const float bpm =
        context.tempoMap != nullptr
            ? static_cast<float>(context.tempoMap->bpmAtSeconds(context.timelineStartSeconds))
            : currentBpm_.load(std::memory_order_relaxed);
    currentBpm_.store(bpm, std::memory_order_relaxed);

    // Every engine, not only the running one: a device that switches engines
    // has to find the user's settings already in the one it switches to.
    for (int engineIndex = 0; engineIndex < static_cast<int>(engines_.size()); ++engineIndex) {
        auto& engine = engines_[static_cast<size_t>(engineIndex)];
        if (engine.instance == nullptr)
            continue;

        for (const auto& harvested : engine.harvested) {
            const int slotIndex = harvested.idx;
            if (slotIndex >= hostSlotCountValue() || harvested.zone == nullptr)
                continue;

            const float normalized = getSlotParameter(slotIndex).currentValue();
            if (!harvested.menuValues.empty()) {
                // A menu's Faust value is whatever the dsp declared for that
                // choice, which is not always its position in the list.
                const int count = static_cast<int>(harvested.menuValues.size());
                const int choice = juce::jlimit(
                    0, count - 1,
                    static_cast<int>(std::lround(normalized * static_cast<float>(count - 1))));
                *harvested.zone =
                    static_cast<FAUSTFLOAT>(harvested.menuValues[static_cast<size_t>(choice)]);
                continue;
            }

            *harvested.zone = static_cast<FAUSTFLOAT>(
                magda::ParameterUtils::normalizedToReal(normalized, infoForSlot(slotIndex)));
        }

        if (engine.projectTempoZone != nullptr)
            *engine.projectTempoZone = static_cast<FAUSTFLOAT>(bpm);

        writeExtraZones(engineIndex);
    }
}

void MagdaCompiledEffect::computeEngine(int engineIndex, DeviceProcessContext& context) {
    if (engineIndex < 0 || engineIndex >= static_cast<int>(engines_.size()))
        return;

    auto& engine = engines_[static_cast<size_t>(engineIndex)];
    if (engine.instance == nullptr || context.audio == nullptr)
        return;

    const int numSamples = context.numSamples;
    const int startSample = context.startSample;
    const int hostChannels = context.audio->getNumChannels();
    const int numInputs = engine.numInputs;
    const int numOutputs = engine.numOutputs;
    if (numSamples <= 0 || hostChannels <= 0 || numOutputs <= 0)
        return;

    if (scratchIn_.getNumChannels() < numInputs || scratchIn_.getNumSamples() < numSamples)
        scratchIn_.setSize(numInputs, numSamples, false, true, true);
    if (scratchOut_.getNumChannels() < numOutputs || scratchOut_.getNumSamples() < numSamples)
        scratchOut_.setSize(numOutputs, numSamples, false, true, true);
    if (static_cast<int>(inPtrs_.size()) < numInputs)
        inPtrs_.resize(static_cast<size_t>(numInputs), nullptr);
    if (static_cast<int>(outPtrs_.size()) < numOutputs)
        outPtrs_.resize(static_cast<size_t>(numOutputs), nullptr);

    // Faust does not allow input and output to alias, so the input is copied
    // out first. Channels the host does not have read as silence.
    for (int channel = 0; channel < numInputs; ++channel) {
        float* destination = scratchIn_.getWritePointer(channel);
        if (channel < hostChannels) {
            const float* source = context.audio->getReadPointer(channel, startSample);
            std::copy(source, source + numSamples, destination);
        } else {
            std::fill(destination, destination + numSamples, 0.0f);
        }
        inPtrs_[static_cast<size_t>(channel)] = destination;
    }

    for (int channel = 0; channel < numOutputs; ++channel)
        outPtrs_[static_cast<size_t>(channel)] =
            channel < hostChannels ? context.audio->getWritePointer(channel, startSample)
                                   : scratchOut_.getWritePointer(channel);

    engine.instance->compute(numSamples, inPtrs_.data(), outPtrs_.data());

    for (int channel = 0; channel < std::min(hostChannels, numOutputs); ++channel) {
        float* out = context.audio->getWritePointer(channel, startSample);
        for (int i = 0; i < numSamples; ++i)
            out[i] = sanitise(out[i]);
    }
}

void MagdaCompiledEffect::processAudio(DeviceProcessContext& context) {
    computeEngine(activeEngine(), context);
}

void MagdaCompiledEffect::process(DeviceProcessContext& context) {
    if (context.audio == nullptr || context.numSamples <= 0)
        return;

    if (context.isPlaying && !wasPlaying_ && resetsOnPlayStart())
        reset();
    wasPlaying_ = context.isPlaying;

    writeZones(context);

    const int engineIndex = activeEngine();
    beforeCompute(context, engineIndex);
    processAudio(context);
    afterCompute(context, engineIndex);
}

}  // namespace magda::daw::audio::compiled
