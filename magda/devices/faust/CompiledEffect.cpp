#include "devices/faust/CompiledEffect.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <utility>

#include "devices/faust/FaustMetadataParser.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"

namespace magda::devices::faust {

namespace {

/// Records every numeric control by the slot its label pins it to, with its menu, gate and role.
class EffectZoneHarvester : public ::UI {
  public:
    struct Control {
        int idx = -1;
        FAUSTFLOAT* zone = nullptr;
        std::vector<std::pair<float, std::string>> menuChoices;
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
        std::string lowerKey = key != nullptr ? key : "";
        std::ranges::transform(lowerKey, lowerKey.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        applyFaustAnnotation(lowerKey, value != nullptr ? value : "", pendingByZone_[zone]);
    }

  private:
    void emit(const char* rawLabel, FAUSTFLOAT* zone) {
        if (zone == nullptr)
            return;

        const auto parsed = parseFaustLabel(rawLabel != nullptr ? rawLabel : "");
        ControlMetadata merged = parsed.metadata;
        if (auto it = pendingByZone_.find(zone); it != pendingByZone_.end()) {
            mergeFaustMetadata(merged, it->second);
            pendingByZone_.erase(it);
        }

        controls.push_back({.idx = merged.slotIndex,
                            .zone = zone,
                            .menuChoices = merged.menuChoices,
                            .gateSlotIndex = merged.gateSlotIndex,
                            .gateNegated = merged.gateNegated,
                            .isProjectTempo = merged.role == FaustControlRole::ProjectTempo});
    }

    std::map<FAUSTFLOAT*, ControlMetadata> pendingByZone_;
};

std::string slugOf(std::string name) {
    for (auto& c : name)
        c = c == ' ' ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return name;
}

}  // namespace

CompiledEffect::CompiledEffect() = default;
CompiledEffect::~CompiledEffect() = default;

::dsp* CompiledEffect::createEngineDsp(int) const {
    return nullptr;
}

void CompiledEffect::initEffect() {
    // A provisional rate makes the device answerable from construction; prepare() brings the
    // real one. The harvest comes first because slotInfos() reads what the dsp declared.
    constexpr int kProvisionalSampleRate = 44100;
    createEngines(kProvisionalSampleRate);

    slotInfo_ = slotInfos();
    applyHarvestedGates();
    buildHostParameters();
    bindSlots();
}

void CompiledEffect::createEngines(int sampleRate) {
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
            harvested.slotIndex = slotForDspIdx(control.idx);
            harvested.zone = control.zone;
            harvested.gateSlotIndex = control.gateSlotIndex;
            harvested.gateNegated = control.gateNegated;
            harvested.isProjectTempo = control.isProjectTempo;

            auto sorted = control.menuChoices;
            std::ranges::sort(sorted,
                              [](const auto& a, const auto& b) { return a.first < b.first; });
            for (auto& [value, label] : sorted) {
                harvested.menuValues.push_back(value);
                harvested.menuLabels.push_back(std::move(label));
            }

            engine.harvested.push_back(std::move(harvested));
        }
    }
}

void CompiledEffect::bindSlots() {
    for (auto& engine : engines_) {
        engine.zonesBySlot.assign(static_cast<size_t>(hostSlotCount()), nullptr);
        for (const auto& harvested : engine.harvested)
            if (harvested.slotIndex >= 0 && harvested.slotIndex < hostSlotCount())
                engine.zonesBySlot[static_cast<size_t>(harvested.slotIndex)] = harvested.zone;
    }
}

void CompiledEffect::applyHarvestedGates() {
    // After slotInfos(): a device's designated initializers zero any gate written before them.
    for (const auto& engine : engines_) {
        for (const auto& harvested : engine.harvested) {
            if (harvested.gateSlotIndex < 0 || harvested.slotIndex < 0 ||
                harvested.slotIndex >= hostSlotCount())
                continue;

            auto& slot = slotInfo_[static_cast<size_t>(harvested.slotIndex)];
            slot.gateSlotIndex = slotForDspIdx(harvested.gateSlotIndex);
            slot.gateNegated = harvested.gateNegated;
        }
    }
}

const CompiledEffect::HarvestedControl* CompiledEffect::harvestedForIdx(int idx) const {
    for (const auto& engine : engines_)
        for (const auto& harvested : engine.harvested)
            if (harvested.idx == idx)
                return &harvested;
    return nullptr;
}

std::vector<std::string> CompiledEffect::menuLabelsForIdx(int idx) const {
    const auto* harvested = harvestedForIdx(idx);
    return harvested != nullptr ? harvested->menuLabels : std::vector<std::string>{};
}

std::vector<float> CompiledEffect::menuValuesForIdx(int idx) const {
    const auto* harvested = harvestedForIdx(idx);
    return harvested != nullptr ? harvested->menuValues : std::vector<float>{};
}

float CompiledEffect::menuValueForChoice(int idx, int choiceIndex) const {
    const auto* harvested = harvestedForIdx(idx);
    if (harvested == nullptr || harvested->menuValues.empty())
        return 0.0f;

    const int last = static_cast<int>(harvested->menuValues.size()) - 1;
    return harvested->menuValues[static_cast<size_t>(std::clamp(choiceIndex, 0, last))];
}

void CompiledEffect::buildHostParameters() {
    slotDomains_.clear();
    slotDomains_.reserve(slotInfo_.size());
    for (int slotIndex = 0; slotIndex < hostSlotCount(); ++slotIndex)
        slotDomains_.push_back(sdk::domainOf(parameterDescriptor(slotIndex)));

    slotValues_ = std::make_unique<std::atomic<float>[]>(slotInfo_.size());
    for (int slotIndex = 0; slotIndex < hostSlotCount(); ++slotIndex)
        slotValues_[static_cast<size_t>(slotIndex)].store(
            sdk::realToNormalized(slotInfo_[static_cast<size_t>(slotIndex)].defaultValue,
                                  domainForSlot(slotIndex)),
            std::memory_order_relaxed);
}

const sdk::ParameterDomain& CompiledEffect::domainForSlot(int slotIndex) const {
    static const sdk::ParameterDomain kEmpty;
    if (slotIndex < 0 || slotIndex >= static_cast<int>(slotDomains_.size()))
        return kEmpty;
    return slotDomains_[static_cast<size_t>(slotIndex)];
}

const SlotInfo& CompiledEffect::slotInfo(int slotIndex) const {
    static const SlotInfo kEmpty;
    if (slotIndex < 0 || slotIndex >= hostSlotCount())
        return kEmpty;
    return slotInfo_[static_cast<size_t>(slotIndex)];
}

std::atomic<float>* CompiledEffect::slotValue(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= hostSlotCount() || slotValues_ == nullptr)
        return nullptr;
    return &slotValues_[static_cast<size_t>(slotIndex)];
}

std::string CompiledEffect::slotId(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= hostSlotCount())
        return {};
    return std::string(slotIdPrefix()) + slugOf(slotInfo_[static_cast<size_t>(slotIndex)].name);
}

sdk::ParameterDescriptor CompiledEffect::parameterDescriptor(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= hostSlotCount())
        return {};

    const auto& slot = slotInfo_[static_cast<size_t>(slotIndex)];
    sdk::ParameterDescriptor descriptor;
    descriptor.index = slotIndex;
    descriptor.stableId = hostSlotId(slotIndex);
    descriptor.name = slot.name;
    descriptor.unit = slot.unit;
    descriptor.scale = slot.scale;
    descriptor.minValue = slot.minValue;
    descriptor.maxValue = slot.maxValue;
    descriptor.defaultValue = slot.defaultValue;
    descriptor.scaleAnchor = std::isfinite(slot.scaleAnchor) ? slot.scaleAnchor : 0.0f;
    descriptor.choices = sdk::choicesFromLabels(slot.choices);
    descriptor.gateSlotIndex = slot.gateSlotIndex;
    descriptor.gateNegated = slot.gateNegated;
    return descriptor;
}

float CompiledEffect::parameterValue(int slotIndex) const {
    const auto* value = slotValue(slotIndex);
    return value != nullptr ? value->load(std::memory_order_relaxed) : 0.0f;
}

void CompiledEffect::setParameterValue(int slotIndex, float normalized) {
    if (auto* value = slotValue(slotIndex))
        value->store(std::clamp(normalized, 0.0f, 1.0f), std::memory_order_relaxed);
}

float CompiledEffect::displayToNormalized(int slotIndex, float displayValue) const {
    if (slotIndex < 0 || slotIndex >= hostSlotCount())
        return displayValue;
    return sdk::realToNormalized(displayValue, domainForSlot(slotIndex));
}

float CompiledEffect::normalizedToDisplay(int slotIndex, float normalizedValue) const {
    if (slotIndex < 0 || slotIndex >= hostSlotCount())
        return normalizedValue;
    return sdk::normalizedToReal(normalizedValue, domainForSlot(slotIndex));
}

float CompiledEffect::slotDisplayValue(int slotIndex) const {
    return normalizedToDisplay(slotIndex, parameterValue(slotIndex));
}

int CompiledEffect::activeEngine() const {
    const int slot = engineSlot();
    if (slot < 0 || engines_.empty())
        return 0;
    return std::clamp(static_cast<int>(std::lround(slotDisplayValue(slot))), 0,
                      static_cast<int>(engines_.size()) - 1);
}

float* CompiledEffect::zoneForIdx(int engineIndex, int idx) const {
    if (engineIndex < 0 || engineIndex >= static_cast<int>(engines_.size()))
        return nullptr;

    for (const auto& harvested : engines_[static_cast<size_t>(engineIndex)].harvested)
        if (harvested.idx == idx)
            return harvested.zone;
    return nullptr;
}

int CompiledEffect::engineInputCount(int engineIndex) const {
    if (engineIndex < 0 || engineIndex >= static_cast<int>(engines_.size()))
        return 0;
    return engines_[static_cast<size_t>(engineIndex)].numInputs;
}

int CompiledEffect::engineOutputCount(int engineIndex) const {
    if (engineIndex < 0 || engineIndex >= static_cast<int>(engines_.size()))
        return 0;
    return engines_[static_cast<size_t>(engineIndex)].numOutputs;
}

sdk::DeviceProperties CompiledEffect::properties() const {
    return {
        .pluginId = devicePluginId(),
        .name = deviceName(),
        .shortName = deviceShortName(),
        .takesMidiInput = wantsMidiInput(),
        .takesAudioInput = true,
        .isSynth = false,
        .producesAudioWithoutInput = producesAudioWithoutInput(),
        .sidechain = sidechainPort(),
        .outputChannelCount = outputChannelCount(),
        .inputChannelCount = inputChannelCount(),
    };
}

int CompiledEffect::latencySamples() const {
    // Round half to even, as the host's rounding always has.
    return static_cast<int>(std::lrint(latencySeconds() * currentSampleRate()));
}

std::int64_t CompiledEffect::tailSamples() const {
    const auto seconds = tailSeconds();
    if (std::isinf(seconds))
        return sdk::kInfiniteTail;
    return static_cast<std::int64_t>(std::ceil(seconds * currentSampleRate()));
}

void CompiledEffect::ensureScratch(int inputs, int outputs, int frames) {
    if (frames > scratchFrames_ || static_cast<int>(scratchIn_.size()) < inputs * frames ||
        static_cast<int>(scratchOut_.size()) < outputs * frames) {
        scratchFrames_ = std::max(scratchFrames_, frames);
        scratchIn_.assign(static_cast<size_t>(std::max(0, inputs) * scratchFrames_), 0.0f);
        scratchOut_.assign(static_cast<size_t>(std::max(0, outputs) * scratchFrames_), 0.0f);
    }
    if (static_cast<int>(inPtrs_.size()) < inputs)
        inPtrs_.resize(static_cast<size_t>(inputs), nullptr);
    if (static_cast<int>(outPtrs_.size()) < outputs)
        outPtrs_.resize(static_cast<size_t>(outputs), nullptr);
}

void CompiledEffect::prepare(const sdk::PrepareContext& context) {
    createEngines(static_cast<int>(context.sampleRate));
    bindSlots();

    int maxInputs = 0;
    int maxOutputs = 0;
    for (const auto& engine : engines_) {
        maxInputs = std::max(maxInputs, engine.numInputs);
        maxOutputs = std::max(maxOutputs, engine.numOutputs);
    }
    scratchFrames_ = 0;
    ensureScratch(maxInputs, maxOutputs, context.maximumBlockSize);

    onPrepare(context.sampleRate, context.maximumBlockSize);
}

void CompiledEffect::release() {
    onRelease();
    scratchIn_.clear();
    scratchOut_.clear();
    scratchFrames_ = 0;
    inPtrs_.clear();
    outPtrs_.clear();
}

void CompiledEffect::reset() {
    for (auto& engine : engines_)
        if (engine.instance)
            engine.instance->instanceClear();
    onReset();
}

void CompiledEffect::writeZones(sdk::ProcessContext& context) {
    const float bpm =
        context.tempoMap != nullptr
            ? static_cast<float>(context.tempoMap->bpmAtSeconds(context.timelineStartSeconds))
            : currentBpm_.load(std::memory_order_relaxed);
    currentBpm_.store(bpm, std::memory_order_relaxed);

    // Every engine, not only the running one, so a switch finds the user's settings in place.
    for (int engineIndex = 0; engineIndex < static_cast<int>(engines_.size()); ++engineIndex) {
        auto& engine = engines_[static_cast<size_t>(engineIndex)];
        if (engine.instance == nullptr)
            continue;

        for (const auto& harvested : engine.harvested) {
            const int slotIndex = harvested.slotIndex;
            if (slotIndex < 0 || slotIndex >= hostSlotCount() || harvested.zone == nullptr)
                continue;

            const float normalized = parameterValue(slotIndex);
            if (!harvested.menuValues.empty()) {
                // A menu's Faust value is what the dsp declared for the choice, not its position.
                const int count = static_cast<int>(harvested.menuValues.size());
                const int choice = std::clamp(
                    static_cast<int>(std::lround(normalized * static_cast<float>(count - 1))), 0,
                    count - 1);
                *harvested.zone =
                    static_cast<FAUSTFLOAT>(harvested.menuValues[static_cast<size_t>(choice)]);
                continue;
            }

            *harvested.zone = static_cast<FAUSTFLOAT>(
                sdk::normalizedToReal(normalized, domainForSlot(slotIndex)));
        }

        if (engine.projectTempoZone != nullptr)
            *engine.projectTempoZone = static_cast<FAUSTFLOAT>(bpm);

        writeExtraZones(engineIndex);
    }
}

void CompiledEffect::computeEngine(int engineIndex, sdk::ProcessContext& context) {
    if (engineIndex < 0 || engineIndex >= static_cast<int>(engines_.size()))
        return;

    auto& engine = engines_[static_cast<size_t>(engineIndex)];
    if (engine.instance == nullptr)
        return;

    const int numSamples = context.numSamples();
    const int hostChannels = context.audio.numChannels();
    const int numInputs = engine.numInputs;
    const int numOutputs = engine.numOutputs;
    if (numSamples <= 0 || hostChannels <= 0 || numOutputs <= 0)
        return;

    ensureScratch(numInputs, numOutputs, numSamples);

    // The key occupies the dsp inputs after the device's own (#2329).
    const auto port = sidechainPort();
    const int keyInputs = port.takesAudio() ? std::min(port.channels, numInputs) : 0;
    const int ownInputs = numInputs - keyInputs;

    // Faust forbids aliasing input and output. Channels the host lacks read as silence.
    for (int channel = 0; channel < numInputs; ++channel) {
        float* destination = scratchIn_.data() + static_cast<size_t>(channel * scratchFrames_);
        const float* source = nullptr;
        if (channel >= ownInputs) {
            // A key narrower than the dsp asked for feeds its last channel to the rest.
            const int numKeys = context.sidechain ? context.sidechain->numChannels() : 0;
            const int key = std::min(channel - ownInputs, numKeys - 1);
            if (key >= 0)
                source = context.sidechain->channel(key);
        } else if (channel < hostChannels) {
            source = context.audio.channel(channel);
        }

        if (source != nullptr)
            std::copy(source, source + numSamples, destination);
        else
            std::fill(destination, destination + numSamples, 0.0f);
        inPtrs_[static_cast<size_t>(channel)] = destination;
    }

    for (int channel = 0; channel < numOutputs; ++channel)
        outPtrs_[static_cast<size_t>(channel)] =
            channel < hostChannels
                ? context.audio.channel(channel)
                : scratchOut_.data() + static_cast<size_t>(channel * scratchFrames_);

    engine.instance->compute(numSamples, inPtrs_.data(), outPtrs_.data());

    for (int channel = 0; channel < std::min(hostChannels, numOutputs); ++channel) {
        float* out = context.audio.channel(channel);
        for (int i = 0; i < numSamples; ++i)
            out[i] = sanitise(out[i]);
    }

    // A fixed output width owns exactly that many channels: above them is the dry input, which
    // must not leak around a device asked to replace it.
    if (outputChannelCount() > 0)
        for (int channel = numOutputs; channel < hostChannels; ++channel)
            std::fill_n(context.audio.channel(channel), numSamples, 0.0f);
}

void CompiledEffect::processAudio(sdk::ProcessContext& context) {
    computeEngine(activeEngine(), context);
}

void CompiledEffect::process(sdk::ProcessContext& context) {
    if (context.numSamples() <= 0)
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

}  // namespace magda::devices::faust
